#include "../wyldtone_fx_common/Dsp.h"
using namespace wyldfx;
namespace {
float sr=48000,detector=0,gain=1;float last[8]={};
constexpr ParameterSpec P[]={
{"threshold","Threshold","dB",-48,0,-18,0.5f},{"ratio","Ratio","x",1,12,3,0.1f},
{"attack","Attack","ms",1,80,12,0.5f},{"release","Release","ms",30,1000,180,1},
{"knee","Knee","dB",0,18,6,0.5f},{"makeup","Makeup","dB",-12,24,3,0.5f},
{"mix","Parallel mix","%",0,100,100,1},{"sidechain_hp","Detector HP","Hz",20,250,80,1}};
OnePoleHP hp[2];
}
extern "C" const char* wyldfx_name(){return "Studio Compressor";}
extern "C" const ParameterSpec* wyldfx_parameters(){return P;}
extern "C" int32_t wyldfx_parameter_count(){return 8;}
extern "C" void wyldfx_prepare(int32_t rate,int32_t,int32_t){sr=rate;detector=0;gain=1;for(auto &f:hp)f.reset();for(float &v:last)v=-999;}
extern "C" void wyldfx_process(const float* in,float* out,int32_t frames,int32_t chans){
    bool changed=false;for(int i=0;i<8;++i){float v=wyldfx_parameter(i);if(v!=last[i]){changed=true;last[i]=v;}}
    if(changed)for(auto &f:hp)f.set(last[7],sr);
    const float aAtt=smooth_alpha(last[2],sr),aRel=smooth_alpha(last[3],sr);
    const float makeup=db_to_linear(last[5]),mix=last[6]/100.0f;
    for(int f=0;f<frames;++f){
        float peak=0;for(int c=0;c<chans;++c)peak=std::max(peak,std::fabs(hp[c].run(finite_sample(in[f*chans+c]))));
        const float a=peak>detector?aAtt:aRel;
        detector=std::max(1.e-8f,a*detector+(1-a)*peak);
        const float db=linear_to_db(detector),over=db-last[0],half=last[4]*0.5f;
        float reduction=0;
        if(last[4]>0 && over>-half && over<half){
            const float knee=over+half;
            reduction=(1-1/last[1])*(knee*knee)/(2*last[4]);
        }else if(over>=half)reduction=(1-1/last[1])*over;
        const float target=db_to_linear(-reduction);
        const float ga=smooth_alpha(target<gain?2.0f:20.0f,sr);
        gain=ga*gain+(1-ga)*target;
        for(int c=0;c<chans;++c){const int j=f*chans+c;const float x=finite_sample(in[j]);out[j]=finite_sample(crossfade(x,x*gain*makeup,mix));}
    }
}
