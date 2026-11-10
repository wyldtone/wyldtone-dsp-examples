#include "../wyldtone_fx_common/Modulation.h"
using namespace wyldfx;
namespace {
float sr=48000,smoothedHz=550;
Lfo lfo; Envelope detector;TptSvf filters[2];OnePoleHP inputHighpass;
constexpr ParameterSpec P[]={
 {"source","Mod source (0 Env/1 LFO/2 Manual)","",0,2,0,1},
 {"filter_mode","Filter (0 Low/1 Band/2 High)","",0,2,1,1},
 {"base_frequency","Start frequency","Hz",90,2600,320,1},
 {"sweep_octaves","Sweep","oct",0,4,2.5f,.05f},
 {"sensitivity","Env sensitivity","dB",-18,24,8,.5f},
 {"attack","Env attack","ms",1,110,9,.5f},
 {"release","Env release","ms",15,750,180,1},
 {"resonance","Resonance","Q",.5f,10,3.2f,.1f},
 {"lfo_rate","LFO rate","Hz",.08f,8,1.4f,.01f},
 {"manual","Manual sweep","%",0,100,50,1},
 {"mix","Mix","%",0,100,85,1},
 {"output","Output","dB",-18,12,0,.5f},
};
}
extern "C" const char* wyldfx_name(){return "Expressive Envelope Wah";}
extern "C" const ParameterSpec* wyldfx_parameters(){return P;}
extern "C" int32_t wyldfx_parameter_count(){return static_cast<int32_t>(sizeof(P)/sizeof(P[0]));}
extern "C" void wyldfx_prepare(int32_t rate,int32_t,int32_t){sr=rate;smoothedHz=550;lfo.reset();detector.reset();inputHighpass.reset();for(auto &f:filters)f.reset();}
extern "C" void wyldfx_process(const float* in,float* out,int32_t frames,int32_t chans){
    const int source=static_cast<int>(wyldfx_parameter(0)+.5f),mode=static_cast<int>(wyldfx_parameter(1)+.5f);
    const float base=wyldfx_parameter(2),depth=wyldfx_parameter(3),gain=db_to_linear(wyldfx_parameter(4));
    const float att=smooth_alpha(wyldfx_parameter(5),sr),rel=smooth_alpha(wyldfx_parameter(6),sr);
    const float q=wyldfx_parameter(7),rate=wyldfx_parameter(8),manual=wyldfx_parameter(9)*.01f;
    const float mix=wyldfx_parameter(10)*.01f,output=db_to_linear(wyldfx_parameter(11));
    const float sweepSmooth=smooth_alpha(2.0f,sr);
    inputHighpass.set(45,sr);
    for(int i=0;i<frames;++i){
        float mono=0;
        for(int c=0;c<chans;++c)mono+=finite_sample(in[i*chans+c]);
        mono/=static_cast<float>(chans);
        // High-pass the raw waveform BEFORE rectification: a high-pass on the
        // rectified envelope would incorrectly erase sustained notes.
        const float env=detector.process(std::fabs(inputHighpass.run(mono)),att,rel);
        const float sweep=source==0 ? clamp(env*gain,0,1) : (source==1 ? (lfo.sample()+1)*.5f : manual);
        const float target=clamp(base*std::exp2(depth*sweep),75.0f,sr*.35f);
        smoothedHz=target+(smoothedHz-target)*sweepSmooth;
        if((i&3)==0)for(int c=0;c<chans;++c)filters[c].configure(smoothedHz,q,sr);
        for(int c=0;c<chans;++c){
            const int j=i*chans+c;const float x=finite_sample(in[j]);
            float lo,bp,hi;filters[c].process(std::tanh(x*1.3f)/1.3f,lo,bp,hi);
            const float wet=mode==0?lo:(mode==2?hi:bp*1.25f);
            out[j]=finite_sample(crossfade(x,wet,mix)*output);
        }
        lfo.advance(rate,sr);
    }
}
