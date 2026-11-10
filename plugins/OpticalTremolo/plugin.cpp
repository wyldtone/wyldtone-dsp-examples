#include "../wyldtone_fx_common/Modulation.h"
using namespace wyldfx;
namespace {
float sr=48000; Lfo lfo;OnePoleLP crossover[2];float smoothGain[2]={1,1};
constexpr ParameterSpec P[]={
 {"mode","Mode (0 Opt/1 Bias/2 Harm/3 Pan)","",0,3,0,1},
 {"rate","Rate","Hz",.08f,15,3.2f,.01f},
 {"depth","Depth","%",0,100,55,1},
 {"waveform","Wave (0 Sin/1 Tri/2 Square)","",0,2,0,1},
 {"stereo_phase","Stereo phase","deg",0,180,0,1},
 {"harmonic_split","Harmonic crossover","Hz",180,2800,850,5},
 {"mix","Mix","%",0,100,100,1},
 {"level","Output","dB",-12,12,0,.5f},
};
}
extern "C" const char* wyldfx_name(){return "Optical / Harmonic Tremolo";}
extern "C" const ParameterSpec* wyldfx_parameters(){return P;}
extern "C" int32_t wyldfx_parameter_count(){return static_cast<int32_t>(sizeof(P)/sizeof(P[0]));}
extern "C" void wyldfx_prepare(int32_t rate,int32_t,int32_t){sr=rate;lfo.reset();for(int c=0;c<2;++c){crossover[c].reset();smoothGain[c]=1;}}
extern "C" void wyldfx_process(const float* in,float* out,int32_t frames,int32_t chans){
    const int mode=static_cast<int>(wyldfx_parameter(0)+.5f),wave=static_cast<int>(wyldfx_parameter(3)+.5f);
    const float rate=wyldfx_parameter(1),depth=wyldfx_parameter(2)*.01f;
    const float phase=wyldfx_parameter(4)/360.0f;
    const float mix=wyldfx_parameter(6)*.01f,master=db_to_linear(wyldfx_parameter(7));
    for(int c=0;c<chans;++c)crossover[c].set(wyldfx_parameter(5),sr);
    const float gainSmooth=smooth_alpha(2.5f,sr);
    for(int i=0;i<frames;++i){
        for(int c=0;c<chans;++c){
            const int j=i*chans+c;const float x=finite_sample(in[j]);
            const float v=lfo.sample(c==0?0:phase,wave);
            float wet=x;
            if(mode==2){ // true split-band harmonic tremolo, opposing modulation
                const float low=crossover[c].run(x),high=x-low;
                const float l=1.0f-depth*(.5f+.5f*v);
                const float h=1.0f-depth*(.5f-.5f*v);
                wet=low*l+high*h;
            } else {
                float gain=1.0f-depth*(.5f+.5f*v);
                if(mode==1)gain=1.0f-depth*(.5f+.5f*v)*(.75f+.25f*v*v);
                if(mode==3 && chans==2){
                    const float pan=(v*depth+1)*.5f;
                    gain=c==0?std::sqrt(2.0f*(1.0f-pan)):std::sqrt(2.0f*pan);
                }
                smoothGain[c]=smoothGain[c]*gainSmooth+gain*(1-gainSmooth);
                wet=x*smoothGain[c];
            }
            out[j]=finite_sample(crossfade(x,wet,mix)*master);
        }
        lfo.advance(rate,sr);
    }
}
