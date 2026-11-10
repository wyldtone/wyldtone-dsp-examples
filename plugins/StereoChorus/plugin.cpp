#include "../wyldtone_fx_common/Dsp.h"
using namespace wyldfx;
namespace {
float sr=48000,phase=0;Ring<8192> delay[2];
constexpr ParameterSpec P[]={
{"rate","Rate","Hz",0.05f,6,0.65f,0.01f},{"depth","Depth","ms",0,8,3.3f,0.1f},
{"pre_delay","Pre-delay","ms",5,24,13,0.5f},{"mix","Mix","%",0,100,35,1},
{"width","Stereo width","%",0,100,85,1},{"feedback","Feedback","%",-45,45,8,1}};
}
extern "C" const char* wyldfx_name(){return "Analog Stereo Chorus";}
extern "C" const ParameterSpec* wyldfx_parameters(){return P;}
extern "C" int32_t wyldfx_parameter_count(){return 6;}
extern "C" void wyldfx_prepare(int32_t rate,int32_t,int32_t){sr=rate;phase=0;for(auto &d:delay)d.reset();}
extern "C" void wyldfx_process(const float* in,float* out,int32_t frames,int32_t chans){
    const float rate=wyldfx_parameter(0),depth=wyldfx_parameter(1)*sr*.001f,base=wyldfx_parameter(2)*sr*.001f;
    const float mix=wyldfx_parameter(3)*.01f,width=wyldfx_parameter(4)*.01f,fb=wyldfx_parameter(5)*.01f;
    for(int f=0;f<frames;++f){
        const float lfoL=std::sin(2*PI*phase),lfoR=std::sin(2*PI*(phase+width*.5f));
        const float m[2]={lfoL,lfoR};
        for(int c=0;c<chans;++c){const int j=f*chans+c;const float x=finite_sample(in[j]);
            const float wet=delay[c].read(std::max(1.0f,base+depth*m[c]));
            delay[c].write(std::tanh(x+wet*fb));
            out[j]=finite_sample(x*(1-mix)+wet*mix);
        }
        phase+=rate/sr;if(phase>=1)phase-=1;
    }
}
