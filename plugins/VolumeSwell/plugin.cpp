#include "../wyldtone_fx_common/Modulation.h"
using namespace wyldfx;
namespace {
float sr=48000,autoLevel=0,manualSmoothed=1;
Envelope detector;bool triggered=false;int silentSamples=0;
constexpr ParameterSpec P[]={
 {"volume","Volume","%",0,100,100,1},
 {"curve","Volume curve (0 Lin/1 Audio/2 Log)","",0,2,1,1},
 {"auto_swell","Auto swell (0 Off/1 On)","",0,1,0,1},
 {"threshold","Trigger threshold","dB",-70,-20,-47,1},
 {"fade_in","Fade in","ms",30,3000,480,10},
 {"fade_out","Fade out","ms",30,3500,900,10},
 {"rearm_silence","Rearm silence","ms",20,900,240,10},
 {"output","Output","dB",-18,12,0,.5f},
};
inline float curve(float t,int mode){
    t=clamp(t,0.0f,1.0f);
    if(mode==0)return t;
    if(mode==2)return std::sqrt(t);
    // Audio taper approximating expression-pedal logarithmic response.
    return (std::pow(10.0f,2.0f*t)-1.0f)/99.0f;
}
}
extern "C" const char* wyldfx_name(){return "Volume Pedal / Auto Swell";}
extern "C" const ParameterSpec* wyldfx_parameters(){return P;}
extern "C" int32_t wyldfx_parameter_count(){return static_cast<int32_t>(sizeof(P)/sizeof(P[0]));}
extern "C" void wyldfx_prepare(int32_t rate,int32_t,int32_t){sr=rate;detector.reset();triggered=false;silentSamples=0;autoLevel=0;manualSmoothed=1;}
extern "C" void wyldfx_process(const float* in,float* out,int32_t frames,int32_t chans){
    const int mode=static_cast<int>(wyldfx_parameter(1)+.5f);
    const bool autoOn=wyldfx_parameter(2)>.5f;
    const float manual=curve(wyldfx_parameter(0)*.01f,mode),threshold=db_to_linear(wyldfx_parameter(3));
    const float attack=1.0f/(std::max(1.0f,wyldfx_parameter(4)*.001f*sr));
    const float release=1.0f/(std::max(1.0f,wyldfx_parameter(5)*.001f*sr));
    const int resetAfter=static_cast<int>(wyldfx_parameter(6)*.001f*sr);
    const float output=db_to_linear(wyldfx_parameter(7)),manualSmooth=smooth_alpha(3.0f,sr);
    const float envRise=smooth_alpha(1.2f,sr),envFall=smooth_alpha(45.0f,sr);
    for(int i=0;i<frames;++i){
        float peak=0;
        for(int c=0;c<chans;++c)peak=std::max(peak,std::fabs(finite_sample(in[i*chans+c])));
        const float env=detector.process(peak,envRise,envFall);
        if(env>threshold){
            silentSamples=0;
            if(!triggered){triggered=true;autoLevel=0;}
        } else if(env<threshold*.6f){
            if(silentSamples<resetAfter+1)++silentSamples;
            if(silentSamples>=resetAfter)triggered=false;
        }
        if(!autoOn){autoLevel=1.0f;}
        else if(triggered){autoLevel=std::min(1.0f,autoLevel+attack);}
        else{autoLevel=std::max(0.0f,autoLevel-release);}
        manualSmoothed=manual+manualSmooth*(manualSmoothed-manual);
        const float g=manualSmoothed*(autoOn?std::sin(autoLevel*PI*.5f):1.0f)*output;
        for(int c=0;c<chans;++c){const int j=i*chans+c;out[j]=finite_sample(finite_sample(in[j])*g);}
    }
}
