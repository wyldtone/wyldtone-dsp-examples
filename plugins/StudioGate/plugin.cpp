#include "../wyldtone_fx_common/Dsp.h"
using namespace wyldfx;
namespace {
float sr=48000,env=0,amp=1,holdLeft=0;bool opened=true;OnePoleHP hp[2];
constexpr ParameterSpec P[]={
{"threshold","Threshold","dB",-85,-15,-53,0.5f},{"hysteresis","Hysteresis","dB",1,18,6,0.5f},
{"attack","Attack","ms",0.5f,25,3,0.5f},{"hold","Hold","ms",0,250,55,1},
{"release","Release","ms",25,1000,220,1},{"range","Reduction","dB",-90,-6,-70,1},
{"detector_hp","Detector HP","Hz",20,350,90,5}};
}
extern "C" const char* wyldfx_name(){return "Natural Guitar Noise Gate";}
extern "C" const ParameterSpec* wyldfx_parameters(){return P;}
extern "C" int32_t wyldfx_parameter_count(){return 7;}
extern "C" void wyldfx_prepare(int32_t rate,int32_t,int32_t){sr=rate;env=0; amp=1;holdLeft=0;opened=true;for(auto &f:hp)f.reset();}
extern "C" void wyldfx_process(const float* in,float* out,int32_t frames,int32_t chans){
    const float openDb=wyldfx_parameter(0),hyst=wyldfx_parameter(1);
    const float aAtt=smooth_alpha(wyldfx_parameter(2),sr),aRel=smooth_alpha(wyldfx_parameter(4),sr);
    const float openThreshold=db_to_linear(openDb),closeThreshold=db_to_linear(openDb-hyst);
    const float minGain=db_to_linear(wyldfx_parameter(5)),holdSamples=wyldfx_parameter(3)*.001f*sr;
    for(int c=0;c<chans;++c)hp[c].set(wyldfx_parameter(6),sr);
    for(int f=0;f<frames;++f){
        float detector=0;
        for(int c=0;c<chans;++c)detector=std::max(detector,std::fabs(hp[c].run(finite_sample(in[f*chans+c]))));
        const float a=detector>env?aAtt:aRel;
        env=a*env+(1-a)*detector;
        if(env>openThreshold){opened=true;holdLeft=holdSamples;}
        else if(opened && env<closeThreshold){if(holdLeft>0)--holdLeft;else opened=false;}
        else if(opened)holdLeft=holdSamples;
        const float target=opened?1.0f:minGain;
        const float gainA=smooth_alpha(opened?wyldfx_parameter(2):wyldfx_parameter(4),sr);
        amp=gainA*amp+(1-gainA)*target;
        for(int c=0;c<chans;++c){const int j=f*chans+c;out[j]=finite_sample(in[j])*amp;}
    }
}
