#include "../wyldtone_fx_common/Dsp.h"
using namespace wyldfx;
namespace {
float sr=48000;Oversample4 oversampler[2];OnePoleHP bassTight[2];OnePoleLP preshape[2],toneLp[2];DcBlock dc[2];Ring<64> dryDelay[2];
constexpr ParameterSpec P[]={
{"drive","Drive","%",0,100,52,1},{"tone","Tone","%",0,100,55,1},
{"tight","Bass tightness","%",0,100,55,1},{"level","Level","dB",-24,12,-5,0.5f},
{"mix","Mix","%",0,100,100,1},{"character","Asymmetry","%",0,100,30,1}};
}
extern "C" const char* wyldfx_name(){return "Warm Analog Drive";}
extern "C" const ParameterSpec* wyldfx_parameters(){return P;}
extern "C" int32_t wyldfx_parameter_count(){return 6;}
extern "C" void wyldfx_prepare(int32_t rate,int32_t,int32_t){sr=rate;for(int c=0;c<2;++c){oversampler[c].prepare();bassTight[c].reset();preshape[c].reset();toneLp[c].reset();dc[c].prepare(sr);dryDelay[c].reset();}}
extern "C" void wyldfx_process(const float* in,float* out,int32_t frames,int32_t chans){
    const float drive=wyldfx_parameter(0)*.01f,tone=wyldfx_parameter(1)*.01f,tight=wyldfx_parameter(2)*.01f;
    const float level=db_to_linear(wyldfx_parameter(3)),mix=wyldfx_parameter(4)*.01f;
    const float asym=wyldfx_parameter(5)*.01f;
    const float g=1.5f+drive*19.0f;
    for(int c=0;c<chans;++c){bassTight[c].set(95+210*tight,sr);preshape[c].set(5200,sr);toneLp[c].set(1800+tone*8500,sr);}
    for(int f=0;f<frames;++f)for(int c=0;c<chans;++c){
        const int j=f*chans+c;const float x=finite_sample(in[j]);
        const float bright=preshape[c].run(bassTight[c].run(x));
        const float wet=oversampler[c].process(bright,[&](float s){
            const float y=g*s+asym*.10f;return (std::tanh(y)-std::tanh(asym*.10f))*0.9f;
        });
        dryDelay[c].write(x);
        const float shaped=dc[c].run(toneLp[c].run(wet))*level;
        out[j]=finite_sample(crossfade(dryDelay[c].read(16),shaped,mix));
    }
}
