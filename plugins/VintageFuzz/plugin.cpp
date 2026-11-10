#include "../wyldtone_fx_common/Dsp.h"
using namespace wyldfx;
namespace {
float sr=48000;Oversample4 oversampler[2];OnePoleHP inputHp[2];OnePoleLP outputLp[2];DcBlock dc[2];Ring<64> dryDelay[2];float sag[2]={0,0};
constexpr ParameterSpec P[]={
{"fuzz","Fuzz","%",0,100,74,1},{"bias","Bias","%",-70,70,-10,1},
{"tone","Tone","%",0,100,50,1},{"sag","Sag","%",0,100,30,1},
{"level","Level","dB",-30,12,-10,0.5f},{"mix","Mix","%",0,100,100,1}};
}
extern "C" const char* wyldfx_name(){return "Vintage Dynamic Fuzz";}
extern "C" const ParameterSpec* wyldfx_parameters(){return P;}
extern "C" int32_t wyldfx_parameter_count(){return 6;}
extern "C" void wyldfx_prepare(int32_t rate,int32_t,int32_t){sr=rate;for(int c=0;c<2;++c){oversampler[c].prepare();inputHp[c].reset();outputLp[c].reset();dc[c].prepare(sr);dryDelay[c].reset();sag[c]=0;}}
extern "C" void wyldfx_process(const float* in,float* out,int32_t frames,int32_t chans){
    const float fuzz=wyldfx_parameter(0)*.01f,bias=wyldfx_parameter(1)*.01f,tone=wyldfx_parameter(2)*.01f;
    const float sagAmt=wyldfx_parameter(3)*.01f,level=db_to_linear(wyldfx_parameter(4)),mix=wyldfx_parameter(5)*.01f;
    const float envCoeff=smooth_alpha(80,sr);
    for(int c=0;c<chans;++c){inputHp[c].set(35+70*fuzz,sr);outputLp[c].set(1400+tone*7500,sr);}
    for(int f=0;f<frames;++f)for(int c=0;c<chans;++c){
        const int j=f*chans+c;const float x=finite_sample(in[j]);
        sag[c]=envCoeff*sag[c]+(1-envCoeff)*std::fabs(x);
        const float drive=(2.5f+fuzz*65)/(1+sagAmt*3.5f*sag[c]);
        const float wet=oversampler[c].process(inputHp[c].run(x),[&](float s){
            const float upper=std::tanh((s+bias*.38f)*drive*1.10f);
            const float lower=std::tanh((s+bias*.38f)*drive*.79f);
            const float center=std::tanh(bias*.38f*drive*1.10f)*.54f+std::tanh(bias*.38f*drive*.79f)*.46f;
            return (upper*.54f+lower*.46f-center)*.88f;
        });
        dryDelay[c].write(x);
        const float y=dc[c].run(outputLp[c].run(wet))*level;
        out[j]=finite_sample(crossfade(dryDelay[c].read(16),y,mix));
    }
}
