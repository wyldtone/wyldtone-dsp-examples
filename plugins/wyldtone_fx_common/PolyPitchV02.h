#pragma once
// Wyldtone polyphonic pitch shifting v0.2: independent bins with spectral
// stable phase propagation and transient-aware resets. C++17, realtime-safe.
// N=1024 (live) or 2048 (studio); 8-fold sqrt-Hann overlap-add.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

namespace wyldfx_poly {
constexpr float PI=3.14159265358979323846f;
constexpr float TWO_PI=2.0f*PI;
constexpr int VOICES=3;
constexpr int CHANNELS=2;
constexpr float RATIOS[VOICES]={0.5f,0.25f,2.0f};
inline float wrap_phase(float phase) { return phase - TWO_PI*std::round(phase/TWO_PI); }
struct Cpx { float re=0,im=0; };

template<int N> struct FFT {
    static_assert(N==1024 || N==2048,"Only validated power-of-two sizes");
    std::array<Cpx,N/2> twiddle{};
    std::array<uint16_t,N> rev{};
    void prepare() {
        for(int k=0;k<N/2;k++){
            float a=-TWO_PI*k/N;
            twiddle[k]={std::cos(a),std::sin(a)};
        }
        int bits=0; while((1<<bits)<N)++bits;
        for(int i=0;i<N;i++){
            int j=0; for(int bit=0;bit<bits;bit++)j=(j<<1)|((i>>bit)&1);
            rev[i]=static_cast<uint16_t>(j);
        }
    }
    void run(std::array<Cpx,N>& a,bool invert) const {
        for(int i=0;i<N;i++)if(rev[i]>i)std::swap(a[i],a[rev[i]]);
        for(int len=2;len<=N;len*=2){
            const int stride=N/len;
            for(int base=0;base<N;base+=len){
                for(int j=0;j<len/2;j++){
                    const Cpx w=twiddle[j*stride];
                    const float wi=invert?-w.im:w.im;
                    const Cpx b=a[base+j+len/2];
                    const float br=w.re*b.re-wi*b.im;
                    const float bi=w.re*b.im+wi*b.re;
                    Cpx &x=a[base+j],&y=a[base+j+len/2];
                    y={x.re-br,x.im-bi}; x={x.re+br,x.im+bi};
                }
            }
        }
        if(invert){const float inv=1.0f/N;for(Cpx &x:a){x.re*=inv;x.im*=inv;}}
    }
};

template<int N> struct Engine {
    static constexpr int HOP=N/8;
    static constexpr int HALF=N/2;
    static constexpr int OUTSIZE=2*N;
    static_assert((OUTSIZE & (OUTSIZE-1))==0,"Output ring must be power of two");

    struct Chan {
        std::array<float,N> in{};
        std::array<std::array<float,OUTSIZE>,VOICES> out{};
        std::array<Cpx,N> spectrum{};
        std::array<float,HALF+1> prevPhase{};
        std::array<float,HALF+1> currentPhase{};
        std::array<float,HALF+1> mag{};
        std::array<float,HALF+1> prevMag{};
        std::array<float,HALF+1> omega{};
        std::array<std::array<float,HALF+1>,VOICES> smoothedOmega{};
        std::array<std::array<uint8_t,HALF+1>,VOICES> frequencyValid{};
        std::array<std::array<float,HALF+1>,VOICES> synthPhase{};
        std::array<float,HALF+1> mappedMag{},mappedOmega{},mappedWeight{};
        std::array<Cpx,HALF+1> onsetPhase{};
        int inputPos=0;
        int onsetCooldown=0;
        bool hasPhase=false;

        void clear() {
            in.fill(0);
            for(auto &v:out)v.fill(0);
            prevPhase.fill(0); currentPhase.fill(0);
            mag.fill(0);prevMag.fill(0);omega.fill(0);
            for(auto &v:synthPhase)v.fill(0);
            for(auto &v:smoothedOmega)v.fill(0);
            for(auto &v:frequencyValid)v.fill(0);
            mappedMag.fill(0);mappedOmega.fill(0);mappedWeight.fill(0);onsetPhase.fill({});
            inputPos=0;onsetCooldown=0;hasPhase=false;
        }

        void render(const FFT<N> &fft,const std::array<float,N>& window,
                    int outPos,const float wetGain[VOICES],float transientFocus) {
            for(int i=0;i<N;i++){
                float x=in[(inputPos+i)&(N-1)]*window[i];
                spectrum[i]={x,0};
            }
            fft.run(spectrum,false);
            float flux=0.0f,energy=0.0f;
            for(int k=0;k<=HALF;k++){
                const float re=spectrum[k].re,im=spectrum[k].im;
                const float m=std::sqrt(re*re+im*im);
                currentPhase[k]=std::atan2(im,re);
                mag[k]=m;
                // Ignore ultra-low/DC energy when identifying pick attacks.
                if(k>=3){flux+=std::max(0.0f,m-prevMag[k]);energy+=m;}
                const float expected=TWO_PI*k*HOP/N;
                const float delta=hasPhase?wrap_phase(currentPhase[k]-prevPhase[k]-expected):0.0f;
                omega[k]=TWO_PI*k/N+delta/HOP;
            }
            // Flux is relative to current energy, so sustained/distorted chords
            // do not reset phase merely because their absolute level is high.
            const bool onset = (hasPhase && energy>0.06f &&
                flux/(energy+1e-9f)>(0.47f-0.19f*transientFocus) && onsetCooldown==0);
            if(onset)onsetCooldown=3;
            else if(onsetCooldown>0)--onsetCooldown;

            // Gather spectral energy and instantaneous frequency at each
            // destination bin. Weighted frequency averaging avoids destructive
            // interference when different source bins collapse onto one bin.
            for(int v=0;v<VOICES;v++){
                // A disabled voice contributes no samples. Skip the expensive
                // remapping, trigonometry and inverse FFT completely.
                // Gains are smoothed by PolyPitchV02Plugin.h; when a voice is
                // re-enabled its phase state is rebuilt below.
                if (wetGain[v] < 0.0001f) {
                    frequencyValid[v].fill(0);
                    continue;
                }
                const float ratio=RATIOS[v];
                mappedMag.fill(0);mappedOmega.fill(0);mappedWeight.fill(0);
                onsetPhase.fill({});
                for(int k=1;k<HALF;k++){
                    const float dest=k*ratio;
                    if(dest>=HALF)break;
                    const int lo=static_cast<int>(dest);
                    const float fraction=dest-lo;
                    const float m=mag[k],o=omega[k]*ratio;
                    const float a=m*(1.0f-fraction),b=m*fraction;
                    mappedMag[lo]+=a; mappedOmega[lo]+=a*o; mappedWeight[lo]+=a;
                    onsetPhase[lo].re+=a*std::cos(currentPhase[k]*ratio);
                    onsetPhase[lo].im+=a*std::sin(currentPhase[k]*ratio);
                    if(lo+1<HALF){
                        mappedMag[lo+1]+=b; mappedOmega[lo+1]+=b*o; mappedWeight[lo+1]+=b;
                        onsetPhase[lo+1].re+=b*std::cos(currentPhase[k]*ratio);
                        onsetPhase[lo+1].im+=b*std::sin(currentPhase[k]*ratio);
                    }
                }
                // Frequency smoothing stabilizes phase progression between
                // adjacent analysis windows without forcibly phase-locking
                // multiple partials (which caused audible sidebands in tests).
                for(int k=1;k<HALF;k++){
                    float localOmega=TWO_PI*k/N;
                    if(mappedWeight[k]>1e-7f)
                        localOmega=mappedOmega[k]/mappedWeight[k];
                    const bool audible=mappedMag[k]>0.001f;
                    if(!audible){ frequencyValid[v][k]=0; }
                    else if(!frequencyValid[v][k] || onset ||
                            std::abs(localOmega-smoothedOmega[v][k])>0.12f) {
                        smoothedOmega[v][k]=localOmega;
                        frequencyValid[v][k]=1;
                    } else {
                        const float smoothing=0.25f+0.3f*transientFocus;
                        smoothedOmega[v][k]+=(localOmega-smoothedOmega[v][k])*smoothing;
                    }
                    const float advance=frequencyValid[v][k]?smoothedOmega[v][k]:localOmega;
                    float &phase=synthPhase[v][k];
                    if(onset && mappedMag[k]>0.001f && transientFocus>0.05f){
                        const float initial=std::atan2(onsetPhase[k].im,onsetPhase[k].re);
                        const float amount=0.30f*transientFocus;
                        phase=wrap_phase(phase+amount*wrap_phase(initial-phase));
                    }
                    phase=wrap_phase(phase+advance*HOP);
                }
                spectrum.fill({});
                for(int k=1;k<HALF;k++){
                    const float phase=synthPhase[v][k];
                    const Cpx x={mappedMag[k]*std::cos(phase),mappedMag[k]*std::sin(phase)};
                    spectrum[k]=x;
                    spectrum[N-k]={x.re,-x.im};
                }
                fft.run(spectrum,true);
                const float normal=2.0f*HOP/N;
                for(int i=0;i<N;i++){
                    const int pos=(outPos+1+i)&(OUTSIZE-1);
                    out[v][pos]+=spectrum[i].re*window[i]*normal;
                }
            }
            for(int k=0;k<=HALF;k++){
                prevPhase[k]=currentPhase[k];prevMag[k]=mag[k];
            }
            hasPhase=true;
        }
    };

    FFT<N> fft;
    std::array<Chan,CHANNELS> chans;
    std::array<float,N> window{};
    std::array<std::array<float,N>,CHANNELS> dry{};
    int inputIndex=0,outIndex=0,dryIndex=0,hopCounter=0;
    void prepare() {
        fft.prepare();
        for(int i=0;i<N;i++){
            const float hann=0.5f-0.5f*std::cos(TWO_PI*(i+0.5f)/N);
            window[i]=std::sqrt(std::max(0.0f,hann));
        }
        for(auto &c:chans)c.clear();
        for(auto &d:dry)d.fill(0);
        outIndex=dryIndex=hopCounter=0;
    }
    // Input/output support in-place processing: load both source samples
    // before writing either output channel.
    void sample(const float* input,float* output,int channels,
                const float wetGain[VOICES],float dryLevel,float master,
                bool aligned,float transientFocus){
        const int count=std::min(channels,CHANNELS);
        float current[CHANNELS]{};
        for(int c=0;c<count;c++){
            current[c]=std::isfinite(input[c])?input[c]:0.0f;
            chans[c].in[chans[c].inputPos]=current[c];
            chans[c].inputPos=(chans[c].inputPos+1)&(N-1);
        }
        if(++hopCounter==HOP){
            hopCounter=0;
            for(int c=0;c<count;c++)chans[c].render(fft,window,outIndex,wetGain,transientFocus);
        }
        const float normal=1.0f/std::max(1.0f,std::sqrt(dryLevel*dryLevel+
            wetGain[0]*wetGain[0]+wetGain[1]*wetGain[1]+wetGain[2]*wetGain[2]));
        for(int c=0;c<count;c++){
            // The STFT wet-envelope group delay is approximately half a
            // window, rather than the entire FFT analysis span.
            const int alignedIndex=(dryIndex+N/2)&(N-1);
            const float oldDry=dry[c][alignedIndex];
            dry[c][dryIndex]=current[c];
            float result=dryLevel*(aligned?oldDry:current[c]);
            for(int v=0;v<VOICES;v++){
                result+=wetGain[v]*chans[c].out[v][outIndex];
                chans[c].out[v][outIndex]=0.0f;
            }
            result*=normal*master;
            if(!std::isfinite(result))result=0.0f;
            // Narrow safety ceiling, no permanent compression at normal gain.
            if(result>0.98f)result=0.98f+0.02f*std::tanh((result-0.98f)/0.02f);
            if(result< -0.98f)result=-0.98f+0.02f*std::tanh((result+0.98f)/0.02f);
            output[c]=result;
        }
        for(int c=count;c<channels;c++)output[c]=input[c];
        outIndex=(outIndex+1)&(OUTSIZE-1);
        dryIndex=(dryIndex+1)&(N-1);
    }
};
} // namespace wyldfx_poly
