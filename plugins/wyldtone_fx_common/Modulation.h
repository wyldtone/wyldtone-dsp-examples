#pragma once
// Shared, small real-time-safe building blocks for Wyldtone FX Suite v0.2.
// No heap allocation, locks, system calls, or exceptions on the audio thread.
#include "Dsp.h"
#include <array>
#include <cmath>

namespace wyldfx {
inline float fract(float v) { return v-std::floor(v); }
inline float lerp(float a,float b,float t) { return a+(b-a)*t; }
inline float lerp_exp(float a,float b,float t) { return a*std::pow(b/a,clamp(t,0.0f,1.0f)); }

struct Lfo {
    float phase=0;
    void reset(float start=0) {phase=fract(start);}
    void advance(float hz,float sr) { phase=fract(phase+clamp(hz,0.0f,sr*.45f)/sr); }
    // Bipolar -1..1. Sine=0, triangle=1, rounded square=2.
    float sample(float offset=0,int wave=0) const {
        const float p=fract(phase+offset);
        if(wave==1) return 1.0f-4.0f*std::fabs(p-0.5f);
        const float s=std::sin(2.0f*PI*p);
        return wave==2 ? std::tanh(4.0f*s) : s;
    }
};

struct Envelope {
    float value=0;
    void reset() {value=0;}
    float process(float amplitude,float a,float r) {
        amplitude=std::fabs(finite_sample(amplitude));
        const float coeff=amplitude>value?a:r;
        value=std::max(0.0f,value*coeff+amplitude*(1.0f-coeff));
        return value;
    }
};

// Topology-preserving-transform state variable filter with independent
// state per channel. Tunable LP / BP / HP at high resonance.
struct TptSvf {
    float s1=0,s2=0;
    float a1=0,a2=0,a3=0,k=1;
    void reset(){s1=s2=0;}
    void configure(float freq,float resonance,float sr) {
        freq=clamp(freq,20.0f,sr*.40f);
        const float g=std::tan(PI*freq/sr);
        k=1.0f/clamp(resonance,.45f,11.0f);
        a1=1.0f/(1.0f+g*(g+k));a2=g*a1;a3=g*a2;
    }
    void process(float x,float &low,float &band,float &high) {
        const float v3=x-s2;
        const float v1=a1*s1+a2*v3;
        const float v2=s2+a2*s1+a3*v3;
        s1=2.0f*v1-s1;s2=2.0f*v2-s2;
        low=v2;band=v1;high=x-k*v1-v2;
    }
};

// 4-point cubic interpolated delay, write head at next unwritten slot.
// Read delay >= 2 samples; this ensures all taps are written history.
template<int Capacity> struct CubicDelay {
    std::array<float,Capacity> data{};
    int pos=0;
    void reset() {data.fill(0.0f);pos=0;}
    float read(float delay) const {
        delay=clamp(delay,2.0f,static_cast<float>(Capacity-4));
        float p=static_cast<float>(pos)-delay;
        if(p<0) p+=static_cast<float>(Capacity);
        const int i=static_cast<int>(p);
        const float t=p-static_cast<float>(i);
        const float m1=data[(i-1+Capacity)%Capacity];
        const float p0=data[i],p1=data[(i+1)%Capacity],p2=data[(i+2)%Capacity];
        // Catmull-Rom interpolation, smooth high-frequency sweeps.
        return p0+0.5f*t*(p1-m1+t*(2.0f*m1-5.0f*p0+4.0f*p1-p2+
                              t*(3.0f*(p0-p1)+p2-m1)));
    }
    void write(float v){data[pos]=finite_sample(v);if(++pos==Capacity)pos=0;}
};

struct OnePoleAllpass {
    float x1=0,y1=0,a=-.5f,target=-.5f;
    void reset(){x1=y1=0;a=target=-.5f;}
    void set_frequency(float f,float sr) {
        f=clamp(f,25.0f,sr*.4f);
        const float g=std::tan(PI*f/sr);
        target=(g-1.0f)/(g+1.0f);
    }
    float process(float x) {
        // Smooth coefficient sweeps between rate-limited control updates.
        a+=(target-a)*.035f;
        const float y=a*x+x1-a*y1;
        x1=x;y1=y;
        return y;
    }
};
}
