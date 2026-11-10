#include "../wyldtone_fx_common/Modulation.h"
using namespace wyldfx;
namespace {
float sr=48000;Lfo lfo;CubicDelay<4096> delay[2];OnePoleHP feedbackHp[2];OnePoleLP feedbackLp[2];
constexpr ParameterSpec P[]={
 {"rate","Rate","Hz",.04f,7,.34f,.01f},
 {"manual","Manual delay","ms",.8f,11,3.0f,.1f},
 {"depth","Sweep depth","ms",0,8,2.5f,.1f},
 {"feedback","Feedback","%",-88,88,42,1},
 {"mix","Mix","%",0,100,43,1},
 {"stereo_phase","Stereo offset","deg",0,180,130,1},
 {"polarity","Wet polarity (0+/1-)","",0,1,0,1},
 {"feedback_tone","Feedback high cut","Hz",1800,16000,9500,50},
 {"output","Output","dB",-12,12,-1,.5f},
};
}
extern "C" const char* wyldfx_name(){return "Stereo Jet Flanger";}
extern "C" const ParameterSpec* wyldfx_parameters(){return P;}
extern "C" int32_t wyldfx_parameter_count(){return static_cast<int32_t>(sizeof(P)/sizeof(P[0]));}
extern "C" void wyldfx_prepare(int32_t rate,int32_t,int32_t){sr=rate;lfo.reset();for(int c=0;c<2;++c){delay[c].reset();feedbackHp[c].reset();feedbackLp[c].reset();}}
extern "C" void wyldfx_process(const float* in,float* out,int32_t frames,int32_t chans){
    const float rate=wyldfx_parameter(0),base=wyldfx_parameter(1),depth=wyldfx_parameter(2);
    const float fb=wyldfx_parameter(3)*.01f,mix=wyldfx_parameter(4)*.01f;
    const float phase=wyldfx_parameter(5)/360.0f,sign=wyldfx_parameter(6)<.5f?1.0f:-1.0f;
    const float cutoff=wyldfx_parameter(7),level=db_to_linear(wyldfx_parameter(8));
    for(int c=0;c<chans;++c){feedbackHp[c].set(24.0f,sr);feedbackLp[c].set(cutoff,sr);}
    for(int i=0;i<frames;++i){
        for(int c=0;c<chans;++c){
            const int j=i*chans+c;const float x=finite_sample(in[j]);
            const float phaseShift=c==0?0:phase;
            // Read uses a high-quality 4-tap cubic interpolator; minimum 2 samples.
            const float delayMs=std::max(.06f,base+depth*lfo.sample(phaseShift));
            const float wet=delay[c].read(std::max(2.0f,delayMs*.001f*sr));
            const float filtered=feedbackLp[c].run(feedbackHp[c].run(wet));
            delay[c].write(std::tanh(x+filtered*fb));
            out[j]=finite_sample(crossfade(x,sign*wet,mix)*level);
        }
        lfo.advance(rate,sr);
    }
}
