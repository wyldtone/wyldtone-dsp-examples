#pragma once
// Shared glue for two independent WASM plugins, no cross-plugin global state.
#ifndef WYLDTONE_POLY_FFT_SIZE
#error "WYLDTONE_POLY_FFT_SIZE must be defined"
#endif
#include "FxShell.h"
#include "PolyPitchV02.h"

using wyldfx::ParameterSpec;
using wyldfx::clamp;
using wyldfx::db_to_linear;
using wyldfx_poly::Engine;

namespace {
Engine<WYLDTONE_POLY_FFT_SIZE> engine;
float gains[4]={};
float outputGain=0.7f;
constexpr ParameterSpec PARAMETERS[]={
    {"dry","Dry level","%",0,100,75,1},
    {"sub1","Octave -1","%",0,100,60,1},
    {"sub2","Octave -2","%",0,100,0,1},
    {"up1","Octave +1","%",0,100,30,1},
    {"dry_align","Dry alignment","mode",0,1,0,1},
    {"level","Output","dB",-18,6,-3,0.5f},
    {"transient","Transient clarity","%",0,100,65,1},
};
float sampleRate=48000.0f;
}
extern "C" const char* wyldfx_name(){return WYLDTONE_POLY_DISPLAY_NAME;}
extern "C" const ParameterSpec* wyldfx_parameters(){return PARAMETERS;}
extern "C" int32_t wyldfx_parameter_count(){return static_cast<int32_t>(sizeof(PARAMETERS)/sizeof(PARAMETERS[0]));}
extern "C" void wyldfx_prepare(int32_t sample_rate,int32_t,int32_t){
    sampleRate=static_cast<float>(std::max(sample_rate,8000));
    engine.prepare();
    for(float &g:gains)g=0;
    outputGain=db_to_linear(-3);
}
extern "C" void wyldfx_process(const float* in,float* out,int32_t frames,int32_t nchannels){
    if(!in||!out||frames<=0||nchannels<=0)return;
    const float targets[4]={
        clamp(wyldfx_parameter(0)*0.01f,0,1),
        clamp(wyldfx_parameter(1)*0.01f,0,1),
        clamp(wyldfx_parameter(2)*0.01f,0,1),
        clamp(wyldfx_parameter(3)*0.01f,0,1),
    };
    const bool dryAligned=wyldfx_parameter(4)>=0.5f;
    const float targetLevel=db_to_linear(clamp(wyldfx_parameter(5),-18,6));
    const float clarity=clamp(wyldfx_parameter(6)*0.01f,0,1);
    const float smoothing=1.0f-std::exp(-1.0f/(0.012f*sampleRate));
    for(int32_t i=0;i<frames;i++){
        for(int v=0;v<4;v++)gains[v]+=(targets[v]-gains[v])*smoothing;
        outputGain+=(targetLevel-outputGain)*smoothing;
        const float wet[3]={gains[1],gains[2],gains[3]};
        engine.sample(in+i*nchannels,out+i*nchannels,nchannels,wet,gains[0],
                      outputGain,dryAligned,clarity);
    }
}
