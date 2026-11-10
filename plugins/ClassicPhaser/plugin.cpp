#include "../wyldtone_fx_common/Modulation.h"
using namespace wyldfx;
namespace {
float sr=48000;Lfo lfo;OnePoleAllpass stage[2][12];OnePoleHP feedbackHp[2];float lastWet[2]={0,0};
constexpr ParameterSpec P[]={
 {"rate","Rate","Hz",.04f,8,.58f,.01f},
 {"center","Center sweep","Hz",100,2600,540,5},
 {"depth","Sweep depth","oct",.2f,4,2,.05f},
 {"stages","Stages (0=4/1=6/2=8/3=12)","",0,3,1,1},
 {"feedback","Resonance","%",-85,85,38,1},
 {"mix","Mix","%",0,100,50,1},
 {"stereo_phase","Stereo phase","deg",0,180,110,1},
 {"output","Output","dB",-12,12,0,.5f},
};
}
extern "C" const char* wyldfx_name(){return "Classic Multi-Stage Phaser";}
extern "C" const ParameterSpec* wyldfx_parameters(){return P;}
extern "C" int32_t wyldfx_parameter_count(){return static_cast<int32_t>(sizeof(P)/sizeof(P[0]));}
extern "C" void wyldfx_prepare(int32_t rate,int32_t,int32_t){sr=rate;lfo.reset();for(int c=0;c<2;++c){lastWet[c]=0;feedbackHp[c].reset();for(auto &s:stage[c])s.reset();}}
extern "C" void wyldfx_process(const float* in,float* out,int32_t frames,int32_t chans){
    const float rate=wyldfx_parameter(0),center=wyldfx_parameter(1),depth=wyldfx_parameter(2);
    const int stageMap[4]={4,6,8,12};const int n=stageMap[static_cast<int>(wyldfx_parameter(3)+.5f)];
    const float fb=wyldfx_parameter(4)*.01f,mix=wyldfx_parameter(5)*.01f;
    const float phaseOffset=wyldfx_parameter(6)/360.0f,level=db_to_linear(wyldfx_parameter(7));
    for(int c=0;c<chans;++c)feedbackHp[c].set(30,sr);
    for(int i=0;i<frames;++i){
        if((i&7)==0){
            // Update frequency on an eight-sample grid, sufficiently fast for
            // even a high-rate LFO while avoiding per-sample tan() overhead.
            for(int c=0;c<chans;++c){
                const float mod=lfo.sample(c==0?0:phaseOffset);
                const float f=center*std::exp2(depth*.5f*mod);
                for(int k=0;k<n;++k){
                    const float spaced=f*std::exp2((static_cast<float>(k)-(n-1)*.5f)*.33f);
                    stage[c][k].set_frequency(spaced,sr);
                }
            }
        }
        for(int c=0;c<chans;++c){
            const int j=i*chans+c;const float x=finite_sample(in[j]);
            float wet=x+std::tanh(feedbackHp[c].run(lastWet[c])*fb);
            for(int k=0;k<n;++k)wet=stage[c][k].process(wet);
            lastWet[c]=clamp(wet,-3.0f,3.0f);
            // In-phase dry/wet creates the notches characteristic of a phaser.
            out[j]=finite_sample(crossfade(x,wet,mix)*level);
        }
        lfo.advance(rate,sr);
    }
}
