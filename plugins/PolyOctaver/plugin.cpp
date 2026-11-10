// Wyldtone Polyphonic Octaver v0.1
// Phase-vocoder, polyphonic spectral pitch shifting (-2, -1, +1 octaves).
// Fixed FFT sizes and static buffers; no pitch tracker, reallocations,
// locks, file access or other blocking work on the audio processing path.
// Unlike zero-crossing frequency dividers this also works on chords.
#include "../wyldtone_fx_common/FxShell.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

using wyldfx::clamp;
using wyldfx::finite_sample;
using wyldfx::db_to_linear;
using wyldfx::ParameterSpec;

namespace {
constexpr int CHANS=2, VOICES=3;
constexpr int FFT_SIZE=4096, HOP=256, HALF=FFT_SIZE/2;
constexpr int OUT_SIZE=8192; // power of two, >= FFT_SIZE+HOP
constexpr float PI=3.14159265358979323846f;
constexpr float TWO_PI=2.0f*PI;
constexpr float RATIOS[VOICES]={0.5f,0.25f,2.0f};

struct Complex {float r=0.0f,i=0.0f;};
struct FFT {
    std::array<Complex,FFT_SIZE/2> twiddle{};
    std::array<uint16_t,FFT_SIZE> reversed{};
    void prepare(){
        for(int k=0;k<FFT_SIZE/2;k++){
            const float a=-TWO_PI*k/FFT_SIZE;
            twiddle[k]={std::cos(a),std::sin(a)};
        }
        for(int i=0;i<FFT_SIZE;i++){
            int rev=0;
            for(int j=0;j<12;j++)rev=(rev<<1)|((i>>j)&1);
            reversed[i]=static_cast<uint16_t>(rev);
        }
    }
    void run(std::array<Complex,FFT_SIZE>& a,bool inverse)const{
        for(int i=0;i<FFT_SIZE;i++)if(reversed[i]>i)std::swap(a[i],a[reversed[i]]);
        for(int len=2;len<=FFT_SIZE;len*=2){
            const int stride=FFT_SIZE/len;
            for(int base=0;base<FFT_SIZE;base+=len){
                for(int j=0;j<len/2;j++){
                    const Complex w=twiddle[j*stride];
                    const float wi=inverse?-w.i:w.i;
                    const Complex v=a[base+j+len/2];
                    const float vr=w.r*v.r-wi*v.i,vi=w.r*v.i+wi*v.r;
                    Complex &u=a[base+j],&z=a[base+j+len/2];
                    z={u.r-vr,u.i-vi};u={u.r+vr,u.i+vi};
                }
            }
        }
        if(inverse){const float scale=1.0f/FFT_SIZE;for(auto &x:a){x.r*=scale;x.i*=scale;}}
    }
};
struct Channel {
    std::array<float,FFT_SIZE> input{};
    std::array<std::array<float,OUT_SIZE>,VOICES> output{};
    std::array<Complex,FFT_SIZE> spectrum{};
    std::array<float,HALF+1> analysisPhase{};
    std::array<float,HALF+1> instantaneousOmega{};
    std::array<std::array<float,HALF+1>,VOICES> synthPhase{};
    std::array<float,HALF+1> binMag{};
    std::array<float,HALF+1> mappedMag{},mappedOmega{},mappedContribution{};
    int inputPos=0;

    void prepare(){
        input.fill(0.0f);
        for(auto &v:output)v.fill(0.0f);
        for(auto &v:synthPhase)v.fill(0.0f);
        analysisPhase.fill(0.0f);
        binMag.fill(0.0f);
        instantaneousOmega.fill(0.0f);
        inputPos=0;
    }
    void analyzeAndSynthesize(const FFT& fft,const std::array<float,FFT_SIZE>& window,
                              int outputPos,float activeGain[VOICES]){
        // Last FFT_SIZE samples oldest first.
        for(int i=0;i<FFT_SIZE;i++){
            const float x=input[(inputPos+i)%FFT_SIZE]*window[i];
            spectrum[i]={x,0.0f};
        }
        fft.run(spectrum,false);
        for(int k=0;k<=HALF;k++){
            const float re=spectrum[k].r,im=spectrum[k].i;
            const float phase=std::atan2(im,re);
            binMag[k]=std::sqrt(re*re+im*im);
            const float expected=TWO_PI*k*HOP/FFT_SIZE;
            float delta=phase-analysisPhase[k]-expected;
            // Robust principal argument: only two corrections are needed;
            // raw atan2 phase plus expected is bounded by +/- 4pi.
            delta-=TWO_PI*std::round(delta/TWO_PI);
            analysisPhase[k]=phase;
            instantaneousOmega[k]=TWO_PI*k/FFT_SIZE+delta/HOP;
        }
        for(int voice=0;voice<VOICES;voice++){
            // Even if muted, advance phase state (avoids a discontinuity
            // when a stored preset unmutes the voice).
            mappedMag.fill(0.0f);
            mappedOmega.fill(0.0f);
            mappedContribution.fill(0.0f);
            const float ratio=RATIOS[voice];
            for(int k=1;k<HALF;k++){
                const float destination=k*ratio;
                if(destination>=HALF)break;
                const int lo=static_cast<int>(destination);
                const float fraction=destination-lo;
                const float magnitude=binMag[k];
                const float omega=instantaneousOmega[k]*ratio;
                const float wlo=magnitude*(1.0f-fraction);
                const float whi=magnitude*fraction;
                mappedMag[lo]+=wlo;
                mappedOmega[lo]+=wlo*omega;
                mappedContribution[lo]+=wlo;
                if(lo+1<=HALF){
                    mappedMag[lo+1]+=whi;
                    mappedOmega[lo+1]+=whi*omega;
                    mappedContribution[lo+1]+=whi;
                }
            }
            if(activeGain[voice]<0.0001f){
                for(int k=1;k<HALF;k++){
                    float omega=TWO_PI*k/FFT_SIZE;
                    if(mappedContribution[k]>1e-7f)
                        omega=mappedOmega[k]/mappedContribution[k];
                    synthPhase[voice][k]+=omega*HOP;
                    if(synthPhase[voice][k]>TWO_PI||synthPhase[voice][k]<-TWO_PI)
                        synthPhase[voice][k]=std::remainder(synthPhase[voice][k],TWO_PI);
                }
                continue;
            }
            for(int k=0;k<FFT_SIZE;k++)spectrum[k]={0.0f,0.0f};
            for(int k=1;k<HALF;k++){
                float omega=TWO_PI*k/FFT_SIZE;
                if(mappedContribution[k]>1e-7f)
                    omega=mappedOmega[k]/mappedContribution[k];
                synthPhase[voice][k]+=omega*HOP;
                if(synthPhase[voice][k]>TWO_PI||synthPhase[voice][k]<-TWO_PI)
                    synthPhase[voice][k]=std::remainder(synthPhase[voice][k],TWO_PI);
                const float amp=mappedMag[k];
                spectrum[k]={amp*std::cos(synthPhase[voice][k]),amp*std::sin(synthPhase[voice][k])};
                spectrum[FFT_SIZE-k]={spectrum[k].r,-spectrum[k].i};
            }
            fft.run(spectrum,true);
            // Full normalized overlap-add. First sample of each new frame
            // is scheduled strictly in the future (no output overwrite).
            // The wet voices have a fixed FFT_SIZE-frame delay.
            const float olaScale=2.0f*HOP/FFT_SIZE;
            for(int i=0;i<FFT_SIZE;i++){
                const int index=(outputPos+1+i)&(OUT_SIZE-1);
                output[voice][index]+=spectrum[i].r*window[i]*olaScale;
            }
        }
    }
};

FFT fft;
std::array<Channel,CHANS> channels;
std::array<float,FFT_SIZE> window;
int outPos=0, hopCounter=0;
float sampleRate=48000.0f;
float gains[4]={0,0,0,0};
float master=0.7f;
constexpr ParameterSpec P[]={
    {"dry", "Dry level", "%",0,100,75,1},
    {"sub1", "Octave -1", "%",0,100,60,1},
    {"sub2", "Octave -2", "%",0,100, 0,1},
    {"up1", "Octave +1", "%",0,100,30,1},
    {"dry_align", "Dry alignment", "mode",0,1,0,1},
    {"level", "Output", "dB",-18,6,-3,0.5f},
};
// No saturating tone coloration; prevent only excursions beyond unity.
float safety(float x){
    if(x>0.98f)return 0.98f+0.02f*std::tanh((x-0.98f)/0.02f);
    if(x< -0.98f)return -0.98f+0.02f*std::tanh((x+0.98f)/0.02f);
    return x;
}
std::array<std::array<float,FFT_SIZE>,CHANS> dryDelay{};
int dryPos=0;
}
extern "C" const char* wyldfx_name(){return "Polyphonic Octaver";}
extern "C" const ParameterSpec* wyldfx_parameters(){return P;}
extern "C" int32_t wyldfx_parameter_count(){return static_cast<int32_t>(sizeof(P)/sizeof(P[0]));}
extern "C" void wyldfx_prepare(int32_t rate,int32_t,int32_t){
    sampleRate=static_cast<float>(std::max(rate,8000));
    fft.prepare();
    for(int i=0;i<FFT_SIZE;i++){
        const float w=0.5f-0.5f*std::cos(TWO_PI*(i+0.5f)/FFT_SIZE);
        window[i]=std::sqrt(std::max(w,0.0f));
    }
    for(auto &channel:channels)channel.prepare();
    for(auto &d:dryDelay)d.fill(0.0f);
    hopCounter=outPos=dryPos=0;
    for(float &g:gains)g=0.0f;
    master=db_to_linear(-3.0f);
}
extern "C" void wyldfx_process(const float* in,float* out,int32_t frames,int32_t nChannels){
    if(!in||!out||frames<=0||nChannels<=0)return;
    const int count=std::min(nChannels,CHANS);
    const float target[4]={
        clamp(wyldfx_parameter(0)*.01f,0.0f,1.0f),
        clamp(wyldfx_parameter(1)*.01f,0.0f,1.0f),
        clamp(wyldfx_parameter(2)*.01f,0.0f,1.0f),
        clamp(wyldfx_parameter(3)*.01f,0.0f,1.0f),
    };
    const bool dryAligned=wyldfx_parameter(4)>=0.5f;
    const float targetMaster=db_to_linear(clamp(wyldfx_parameter(5),-18,6));
    const float smoothing=1.0f-std::exp(-1.0f/(0.012f*sampleRate));
    for(int i=0;i<frames;i++){
        for(int v=0;v<4;v++)gains[v]+=(target[v]-gains[v])*smoothing;
        master+=(targetMaster-master)*smoothing;
        float active[VOICES]={gains[1],gains[2],gains[3]};
        float source[CHANS]={};
        for(int ch=0;ch<count;ch++){
            const float x=finite_sample(in[i*nChannels+ch]);
            source[ch]=x;
            auto& st=channels[ch];
            st.input[st.inputPos]=x;
            if(++st.inputPos==FFT_SIZE)st.inputPos=0;
        }
        // The hop boundary is fixed in sample time and never depends on
        // the host CPAL callback length or the configured DSP block size.
        if(++hopCounter==HOP){
            hopCounter=0;
            for(int ch=0;ch<count;ch++)
                channels[ch].analyzeAndSynthesize(fft,window,outPos,active);
        }
        float normalizer=1.0f/std::max(1.0f,std::sqrt(
            gains[0]*gains[0]+gains[1]*gains[1]+gains[2]*gains[2]+gains[3]*gains[3]));
        for(int ch=0;ch<count;ch++){
            auto& st=channels[ch];
            const float delayedDry=dryDelay[ch][dryPos];
            dryDelay[ch][dryPos]=source[ch];
            float value=gains[0]*(dryAligned?delayedDry:source[ch]);
            for(int v=0;v<VOICES;v++){
                value+=gains[v+1]*st.output[v][outPos];
                st.output[v][outPos]=0.0f;
            }
            out[i*nChannels+ch]=finite_sample(safety(value*normalizer*master));
        }
        for(int ch=count;ch<nChannels;ch++)out[i*nChannels+ch]=finite_sample(in[i*nChannels+ch]);
        if(++outPos==OUT_SIZE)outPos=0;
        if(++dryPos==FFT_SIZE)dryPos=0;
    }
}
