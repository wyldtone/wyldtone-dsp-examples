#include "../wyldtone_fx_common/Dsp.h"
using namespace wyldfx;
namespace {
float sr=48000;Biquad lo[2],mid[2],hi[2];float last[5]={10000,10000,10000,10000,10000};
constexpr ParameterSpec P[]={{"low","Low","dB",-15,15,0,0.1f},{"mid","Mid","dB",-15,15,0,0.1f},{"mid_frequency","Mid frequency","Hz",200,3200,850,1},{"high","High","dB",-15,15,0,0.1f},{"level","Level","dB",-18,12,0,0.1f}};
}
extern "C" const char* wyldfx_name(){return "Studio 3-Band EQ";}
extern "C" const ParameterSpec* wyldfx_parameters(){return P;}
extern "C" int32_t wyldfx_parameter_count(){return 5;}
extern "C" void wyldfx_prepare(int32_t rate,int32_t,int32_t){sr=rate;for(int c=0;c<2;++c){lo[c].reset();mid[c].reset();hi[c].reset();}for(float &v:last)v=10000;}
extern "C" void wyldfx_process(const float* in,float* out,int32_t frames,int32_t chans){
    bool changed=false;for(int i=0;i<5;++i){float v=wyldfx_parameter(i);if(v!=last[i]){last[i]=v;changed=true;}}
    if(changed)for(int c=0;c<2;++c){lo[c].set(0,sr,160,last[0]);mid[c].set(1,sr,last[2],last[1],0.80f);hi[c].set(2,sr,4300,last[3]);}
    const float level=db_to_linear(last[4]);
    for(int f=0;f<frames;++f)for(int c=0;c<chans;++c){const int j=f*chans+c;float x=finite_sample(in[j]);out[j]=finite_sample(hi[c].run(mid[c].run(lo[c].run(x)))*level);}
}
