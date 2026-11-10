#include "../wyldtone_fx_common/Dsp.h"
using namespace wyldfx;
namespace {
float sr=48000;Oversample4 oversampler[2];OnePoleHP tightHp[2];OnePoleLP preLp[2],postLp[2];DcBlock dc[2];Ring<64> dryDelay[2];
constexpr ParameterSpec P[]={
{"gain","Gain","%",0,100,65,1},{"tight","Tight","%",0,100,60,1},
{"tone","Tone","%",0,100,48,1},{"body","Body","%",0,100,50,1},
{"level","Level","dB",-30,12,-9,0.5f},{"mix","Mix","%",0,100,100,1}};
}
extern "C" const char* wyldfx_name(){return "Modern Amp Distortion";}
extern "C" const ParameterSpec* wyldfx_parameters(){return P;}
extern "C" int32_t wyldfx_parameter_count(){return 6;}
extern "C" void wyldfx_prepare(int32_t rate,int32_t,int32_t){sr=rate;for(int c=0;c<2;++c){oversampler[c].prepare();tightHp[c].reset();preLp[c].reset();postLp[c].reset();dc[c].prepare(sr);dryDelay[c].reset();}}
extern "C" void wyldfx_process(const float* in,float* out,int32_t frames,int32_t chans){
    const float gain=wyldfx_parameter(0)*.01f,tight=wyldfx_parameter(1)*.01f,tone=wyldfx_parameter(2)*.01f;
    const float body=wyldfx_parameter(3)*.01f,level=db_to_linear(wyldfx_parameter(4));
    const float mix=wyldfx_parameter(5)*.01f;
    for(int c=0;c<chans;++c){tightHp[c].set(60+370*tight,sr);preLp[c].set(6500,sr);postLp[c].set(1900+tone*9000,sr);}
    for(int f=0;f<frames;++f)for(int c=0;c<chans;++c){
        const int j=f*chans+c;const float x=finite_sample(in[j]);
        const float pre=preLp[c].run((1-body*.38f)*tightHp[c].run(x)+body*.38f*x);
        const float wet=oversampler[c].process(pre,[&](float s){
            const float stage1=std::tanh(s*(2+gain*24));
            const float stage2=std::tanh(stage1*(1.05f+gain*2.8f)+.07f);
            return (stage2-std::tanh(.07f))*.86f;
        });
        dryDelay[c].write(x);
        const float y=dc[c].run(postLp[c].run(wet))*level;
        out[j]=finite_sample(crossfade(dryDelay[c].read(16),y,mix));
    }
}
