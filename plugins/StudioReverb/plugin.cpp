#include "../wyldtone_fx_common/Dsp.h"
using namespace wyldfx;
namespace {
float sr=48000;OnePoleHP inputHp[2];OnePoleLP inputLp[2];Ring<10000> predelay[2];
static constexpr int CN=8,AN=4;
static constexpr int cL[CN]={1557,1617,1491,1422,1277,1356,1188,1116};
static constexpr int aL[AN]={225,556,441,341};
struct Comb {Ring<7000> ring;float damp=0;};
struct Allpass {Ring<2000> ring;};
Comb comb[2][CN];Allpass allpass[2][AN];
constexpr ParameterSpec P[]={
{"decay","Decay","s",0.35f,9,2.4f,0.05f},{"damping","Damping","%",0,100,55,1},
{"mix","Mix","%",0,100,22,1},{"pre_delay","Pre-delay","ms",0,85,15,1},
{"high_cut","High cut","Hz",2200,15000,9000,100},{"low_cut","Low cut","Hz",20,800,150,10},
{"width","Stereo width","%",0,100,80,1}};
}
extern "C" const char* wyldfx_name(){return "Studio Space Reverb";}
extern "C" const ParameterSpec* wyldfx_parameters(){return P;}
extern "C" int32_t wyldfx_parameter_count(){return 7;}
extern "C" void wyldfx_prepare(int32_t rate,int32_t,int32_t){sr=rate;for(int c=0;c<2;++c){inputHp[c].reset();inputLp[c].reset();predelay[c].reset();for(auto &x:comb[c]){x.ring.reset();x.damp=0;}for(auto &x:allpass[c])x.ring.reset();}}
extern "C" void wyldfx_process(const float* in,float* out,int32_t frames,int32_t chans){
    const float decay=wyldfx_parameter(0),damp=wyldfx_parameter(1)*.01f,mix=wyldfx_parameter(2)*.01f;
    const float pred=wyldfx_parameter(3)*sr*.001f,width=wyldfx_parameter(6)*.01f;
    for(int c=0;c<2;++c){inputHp[c].set(wyldfx_parameter(5),sr);inputLp[c].set(wyldfx_parameter(4),sr);}
    float feedbackGain[2][CN];
    float lengths[2][CN];
    float allpassLengths[2][AN];
    for(int c=0;c<2;++c){
        for(int i=0;i<CN;++i){
            lengths[c][i]=(cL[i]+(c?23:0))*sr/48000.0f;
            feedbackGain[c][i]=std::pow(10.0f,-3.0f*lengths[c][i]/(sr*std::max(0.1f,decay)));
        }
        for(int i=0;i<AN;++i) allpassLengths[c][i]=(aL[i]+(c?17:0))*sr/48000.0f;
    }
    for(int f=0;f<frames;++f){
        float wet[2];float dry[2]={finite_sample(in[f*chans]),chans>1?finite_sample(in[f*chans+1]):finite_sample(in[f*chans])};
        for(int c=0;c<2;++c){
            predelay[c].write(inputLp[c].run(inputHp[c].run(dry[c])));
            const float signal=predelay[c].read(std::max(1.0f,pred));
            float sum=0;
            for(int i=0;i<CN;++i){
                const float len=lengths[c][i];
                const float tap=comb[c][i].ring.read(len);
                const float a=0.1f+0.75f*damp;
                comb[c][i].damp=(1-a)*tap+a*comb[c][i].damp;
                comb[c][i].ring.write(signal*0.10f+comb[c][i].damp*feedbackGain[c][i]);
                sum+=tap;
            }
            sum*=0.25f;
            for(int i=0;i<AN;++i){
                const float len=allpassLengths[c][i];
                const float delayed=allpass[c][i].ring.read(len);
                const float v=sum+0.5f*delayed;
                allpass[c][i].ring.write(v);
                sum=delayed-0.5f*v;
            }
            wet[c]=sum;
        }
        for(int c=0;c<chans;++c){const float mid=.5f*(wet[0]+wet[1]),side=.5f*(wet[0]-wet[1]);
            const float w=c==0?mid+side*width:mid-side*width;
            out[f*chans+c]=finite_sample(crossfade(dry[c],w,mix));
        }
    }
}
