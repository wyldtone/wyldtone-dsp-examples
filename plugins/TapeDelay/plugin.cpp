#include "../wyldtone_fx_common/Dsp.h"
using namespace wyldfx;
namespace {
float sr=48000,currentDelay=24000;Ring<192000> delay[2];OnePoleLP lp[2];OnePoleHP hp[2];
constexpr ParameterSpec P[]={
{"time","Delay time","ms",40,1600,390,1},{"feedback","Feedback","%",0,90,38,1},
{"mix","Mix","%",0,100,28,1},{"tone","Repeat tone","Hz",1200,12000,5500,50},
{"low_cut","Repeat low cut","Hz",20,800,180,10},{"ping_pong","Ping pong","%",0,100,70,1},
{"saturation","Saturation","%",0,100,15,1}};
}
extern "C" const char* wyldfx_name(){return "Stereo Tape Delay";}
extern "C" const ParameterSpec* wyldfx_parameters(){return P;}
extern "C" int32_t wyldfx_parameter_count(){return 7;}
extern "C" void wyldfx_prepare(int32_t rate,int32_t,int32_t){sr=rate;currentDelay=.390f*sr;for(auto &d:delay)d.reset();for(auto &f:lp)f.reset();for(auto &f:hp)f.reset();}
extern "C" void wyldfx_process(const float* in,float* out,int32_t frames,int32_t chans){
    const float target=wyldfx_parameter(0)*sr*.001f,fb=wyldfx_parameter(1)*.01f,mix=wyldfx_parameter(2)*.01f;
    const float ping=wyldfx_parameter(5)*.01f,drive=wyldfx_parameter(6)*.01f;
    for(int c=0;c<2;++c){lp[c].set(wyldfx_parameter(3),sr);hp[c].set(wyldfx_parameter(4),sr);}
    const float smoothing=smooth_alpha(60,sr);
    for(int f=0;f<frames;++f){
        currentDelay=smoothing*currentDelay+(1-smoothing)*target;
        float input[2]={finite_sample(in[f*chans]),chans>1?finite_sample(in[f*chans+1]):finite_sample(in[f*chans])};
        float tap[2]={delay[0].read(currentDelay),delay[1].read(currentDelay)};
        float next[2];
        float filtered[2]={hp[0].run(lp[0].run(tap[0])),hp[1].run(lp[1].run(tap[1]))};
        for(int c=0;c<2;++c){
            const float feedbackSignal=(1-ping)*filtered[c]+ping*filtered[1-c];
            next[c]=input[c]+std::tanh(feedbackSignal*fb*(1+drive*2))/(1+drive*1.1f);
        }
        delay[0].write(next[0]);delay[1].write(next[1]);
        for(int c=0;c<chans;++c)out[f*chans+c]=finite_sample(crossfade(input[c],tap[c],mix));
    }
}
