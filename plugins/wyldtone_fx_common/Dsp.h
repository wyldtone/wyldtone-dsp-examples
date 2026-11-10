#pragma once
#include "FxShell.h"
#include <array>
#include <cmath>
#include <cstddef>

namespace wyldfx {
constexpr float PI = 3.14159265358979323846f;
inline float smooth_alpha(float ms, float sr) {
    return std::exp(-1000.0f / (std::max(0.01f, ms) * sr));
}
inline float onepole_alpha(float hz, float sr) {
    return std::exp(-2.0f * PI * clamp(hz, 2.0f, sr * 0.45f) / sr);
}
struct OnePoleLP {
    float state = 0, a = 0;
    void set(float hz, float sr) { a = onepole_alpha(hz, sr); }
    float run(float x) { state = x + a * (state - x); return state; }
    void reset() { state = 0; }
};
struct OnePoleHP {
    OnePoleLP lp;
    void set(float hz, float sr) { lp.set(hz, sr); }
    float run(float x) { return x - lp.run(x); }
    void reset() { lp.reset(); }
};
struct DcBlock {
    float x1=0, y1=0, r=0.995f;
    void prepare(float sr, float hz=18.0f) {r=onepole_alpha(hz,sr); x1=0; y1=0;}
    float run(float x) {float y=x-x1+r*y1; x1=x; y1=y; return y;}
};
struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
    void reset() {z1=z2=0;}
    float run(float x) {const float y=b0*x+z1; z1=b1*x-a1*y+z2; z2=b2*x-a2*y; return y;}
    void set(int type,float sr,float hz,float db,float q=0.70710678f) {
        hz=clamp(hz,15.0f,sr*0.44f);
        const double A=std::pow(10.0,static_cast<double>(db)/40.0);
        const double w=2.0*static_cast<double>(PI)*hz/sr;
        const double sn=std::sin(w),cs=std::cos(w);
        const double alpha=sn/(2.0*std::max(0.2f,q));
        const double two=2.0*std::sqrt(A)*alpha;
        double B0,B1,B2,A0,A1,A2;
        if(type==0) { // low shelf
            B0=A*((A+1)-(A-1)*cs+two); B1=2*A*((A-1)-(A+1)*cs);
            B2=A*((A+1)-(A-1)*cs-two); A0=(A+1)+(A-1)*cs+two;
            A1=-2*((A-1)+(A+1)*cs); A2=(A+1)+(A-1)*cs-two;
        } else if(type==2) { // high shelf
            B0=A*((A+1)+(A-1)*cs+two); B1=-2*A*((A-1)+(A+1)*cs);
            B2=A*((A+1)+(A-1)*cs-two); A0=(A+1)-(A-1)*cs+two;
            A1=2*((A-1)-(A+1)*cs); A2=(A+1)-(A-1)*cs-two;
        } else { // peaking
            B0=1+alpha*A; B1=-2*cs; B2=1-alpha*A;
            A0=1+alpha/A; A1=-2*cs; A2=1-alpha/A;
        }
        b0=static_cast<float>(B0/A0);b1=static_cast<float>(B1/A0);
        b2=static_cast<float>(B2/A0);a1=static_cast<float>(A1/A0);a2=static_cast<float>(A2/A0);
    }
};
// 4x band-limited, causal oversampler. 16 original-sample taps per interpolation
// phase; 65 oversampled taps for anti-alias filtering before downsampling.
// Fixed memory: no allocations, mutexes or I/O on the audio thread.
struct Oversample4 {
    static constexpr int UPS=4, TAPS=16, DOWN=65;
    std::array<float,TAPS> history{};
    std::array<float,DOWN> recent{};
    std::array<std::array<float,TAPS>,UPS> up{};
    std::array<float,DOWN> down{};
    int hpos=0, dpos=0;
    void prepare(){
        history.fill(0);recent.fill(0);hpos=dpos=0;
        for(int p=0;p<UPS;++p){
            float sum=0;
            for(int k=0;k<TAPS;++k){
                const float x=static_cast<float>(k)-7.5f+static_cast<float>(p)/UPS;
                const float sn=std::fabs(x)<1.e-6f?1.0f:std::sin(PI*x)/(PI*x);
                const float window=0.42f-0.5f*std::cos(2*PI*k/(TAPS-1))
                                   +0.08f*std::cos(4*PI*k/(TAPS-1));
                up[p][k]=sn*window;sum+=up[p][k];
            }
            for(float &v:up[p])v/=sum;
        }
        float sum=0;
        for(int k=0;k<DOWN;++k){
            const float x=static_cast<float>(k)-(DOWN-1)*0.5f;
            const float v=std::fabs(x)<1.e-6f?0.25f:std::sin(PI*0.25f*x)/(PI*x);
            const float win=0.42f-0.5f*std::cos(2*PI*k/(DOWN-1))
                           +0.08f*std::cos(4*PI*k/(DOWN-1));
            down[k]=v*win;sum+=down[k];
        }
        for(float &v:down)v/=sum;
    }
    template <typename Shaper> float process(float x,Shaper&& shape){
        history[hpos]=x;
        for(int p=0;p<UPS;++p){
            float y=0;
            for(int k=0;k<TAPS;++k){
                const int idx=(hpos-k+TAPS)%TAPS;
                y+=history[idx]*up[p][k];
            }
            recent[dpos]=shape(y);
            if(++dpos==DOWN)dpos=0;
        }
        if(++hpos==TAPS)hpos=0;
        float out=0;
        for(int k=0;k<DOWN;++k) {
            const int idx=(dpos+DOWN-1-k)%DOWN;
            out+=recent[idx]*down[k];
        }
        return out;
    }
};
// Fixed-size fractional delay line with interpolated taps.
template<int N> struct Ring {
    std::array<float,N> data{};
    int pos=0;
    void reset(){data.fill(0);pos=0;}
    float read(float frames) const {
        frames=clamp(frames,1.0f,static_cast<float>(N-3));
        float offset=static_cast<float>(pos)-frames;
        if(offset<0)offset+=static_cast<float>(N);
        const int a=static_cast<int>(offset),b=(a+1)%N;
        const float f=offset-a;
        return data[a]+(data[b]-data[a])*f;
    }
    void write(float v) {data[pos]=v; if(++pos==N)pos=0;}
};
inline float soft_saturate(float x){return std::tanh(x);}
inline float crossfade(float dry,float wet,float mix) {
    mix=clamp(mix,0.0f,1.0f);return dry*(1.0f-mix)+wet*mix;
}
}
