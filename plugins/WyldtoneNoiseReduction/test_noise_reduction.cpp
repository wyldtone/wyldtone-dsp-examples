// Native smoke/integration checks. No Emscripten required.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

extern "C" void plugin_init(int32_t,int32_t,int32_t);
extern "C" void plugin_parameter_set(int32_t,float);
extern "C" float plugin_parameter_get(int32_t);
extern "C" uint8_t plugin_pending_events();
extern "C" void wyldfx_process(const float*,float*,int32_t,int32_t);
extern "C" int32_t plugin_state_capture();
extern "C" uint32_t plugin_state_version();
extern "C" const void* wyldfx_test_profile_state_bytes();
extern "C" void wyldfx_test_forget_profile();
extern "C" void plugin_state_commit_load(int32_t);

namespace {
constexpr int SR=48000;
constexpr int DELAY=511;
constexpr float PI=3.14159265358979323846f;
uint32_t rng=0x4ace22bd;
float randnoise() {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (static_cast<float>(rng & 0x00FFFFFF) / 8388608.0f) - 1.0f;
}
void check(bool pass, const char* what) {
    if(!pass){std::fprintf(stderr,"FAILED: %s\n",what);std::exit(1);}
}
void run(const std::vector<float>& input,std::vector<float>& output,int channels,int block=127) {
    const int samples=static_cast<int>(input.size()/channels);
    output.assign(input.size(),0);
    for(int f=0;f<samples;f+=block){
        const int frames=std::min(block,samples-f);
        wyldfx_process(input.data()+f*channels,output.data()+f*channels,frames,channels);
    }
}
float rms(const std::vector<float>& a,int first,int last,int channel=0,int nch=1) {
    double sq=0;int count=0;
    for(int i=first;i<last;++i){const float sample=a[static_cast<size_t>(i)*nch+channel];sq+=sample*sample;++count;}
    return std::sqrt(sq/std::max(1,count));
}
}
int main(){
    check(plugin_state_version()==1,"state version");
    plugin_init(SR,2,128);
    // Identity check: with no noise profile, do not modify the frequency response.
    const int length=SR*2;
    std::vector<float> wave(length*2),processed;
    for(int i=0;i<length;++i){
        wave[i*2]=.23f*std::sin(2*PI*173*i/SR)+.13f*std::sin(2*PI*4832*i/SR);
        wave[i*2+1]=.33f*std::sin(2*PI*377*i/SR);
    }
    run(wave,processed,2);
    double total=0;int count=0;
    for(int i=1500;i<length;++i) for(int ch=0;ch<2;++ch){
        const float error=processed[i*2+ch]-wave[(i-DELAY)*2+ch];
        total += error*error;++count;
    }
    const float error=std::sqrt(total/count);
    std::printf("Identity residual RMS: %.8f\n",error);
    check(error<0.0002f,"transparent when no profile");

    // Learn with realistic quiet broadband noise (at the post-amp tap).
    plugin_init(SR,2,128);
    plugin_parameter_set(0,1); // Learn Noise button.
    plugin_parameter_set(1,1); // full strength
    plugin_parameter_set(2,0.8f); // 24 dB maximum attenuation
    const int noise_length=SR*5;
    std::vector<float> noise(noise_length*2),filtered;
    for(int i=0;i<noise_length;++i){
        const float value=randnoise()*.012f;
        noise[i*2]=value; noise[i*2+1]=value;
    }
    run(noise,filtered,2,97);
    check(plugin_parameter_get(0)<0.5f,"learn control auto resets after 2.5 seconds");
    plugin_parameter_set(0,0);
    check((plugin_pending_events()&2)!=0,"host is notified of learned opaque profile");
    const int32_t saved_size = plugin_state_capture();
    check(saved_size>1000,"opaque noise profile serialized");
    std::vector<uint8_t> saved(saved_size);
    std::memcpy(saved.data(), wyldfx_test_profile_state_bytes(), saved.size());
    const float input_rms=rms(noise,SR*3,SR*5,0,2);
    const float output_rms=rms(filtered,SR*3,SR*5,0,2);
    const float improvement=20*std::log10(std::max(output_rms,1e-12f)/input_rms);
    std::printf("Noise RMS input=%.6f output=%.6f reduction=%.2f dB\n",input_rms,output_rms,improvement);
    check(improvement < -5.0f,"reduces stationary learned noise");

    // Verify that the *saved profile* can be restored after an instance reset.
    plugin_init(SR,2,128);
    wyldfx_test_forget_profile();
    plugin_parameter_set(1,1);
    plugin_parameter_set(2,0.8f);
    std::memcpy(const_cast<void*>(wyldfx_test_profile_state_bytes()),saved.data(),saved.size());
    plugin_state_commit_load(saved_size);
    std::vector<float> restored_signal(SR*2), restored_output;
    for(int i=0;i<SR;++i) {
        const float v=randnoise()*.012f;
        restored_signal[i*2]=restored_signal[i*2+1]=v;
    }
    run(restored_signal, restored_output,2,111);
    const float restored_db = 20*std::log10(rms(restored_output,SR/2,SR,0,2) /
                                           rms(restored_signal,SR/2,SR,0,2));
    std::printf("Restored profile noise reduction: %.2f dB\n",restored_db);
    check(restored_db<-5,"restored noise profile is active");

    // v32 regression: a moderate setting must suppress significantly more than
    // v31, where the linear blend limited 45% / 12 dB to only ~3.6 dB.
    plugin_init(SR, 2, 128);
    std::memcpy(const_cast<void*>(wyldfx_test_profile_state_bytes()),saved.data(),saved.size());
    plugin_state_commit_load(saved_size);
    plugin_parameter_set(0, 0); // Learn disabled
    plugin_parameter_set(1, 0.45f); // 45% strength
    plugin_parameter_set(2, 0.4f); // max 12 dB
    plugin_parameter_set(3, 0.95f); // transient protection
    plugin_parameter_set(4, 1.0f); // mix 100%
    std::vector<float> moderate_output;
    run(noise, moderate_output, 2, 128);
    const float moderate_db = 20*std::log10(
        rms(moderate_output, SR*3, SR*5, 0, 2) / rms(noise, SR*3, SR*5, 0, 2));
    std::printf("Moderate 45%% / max 12 dB: %.2f dB\n", moderate_db);
    check(moderate_db < -5.0f && moderate_db > -12.5f,
          "moderate strength gives meaningful and bounded reduction");

    // Mix=0 is time-aligned dry audio even when an existing profile is active.
    plugin_init(SR, 2, 128);
    plugin_parameter_set(1, 0.45f);
    plugin_parameter_set(2, 0.4f);
    plugin_parameter_set(4, 0.0f);
    std::vector<float> bypassed;
    run(noise, bypassed, 2, 128);
    double err2=0; int n2=0;
    for (int i=1200;i<SR*5;++i) {
        const float e = bypassed[2*i] - noise[2*(i-DELAY)];
        err2 += e*e; ++n2;
    }
    check(std::sqrt(err2/n2) < 0.00001,
          "mix 0 is transparent with valid noise profile");

    // A loud note must be substantially preserved, even with background hiss.
    const int note_length=SR;
    std::vector<float> note(note_length*2),note_out;
    for(int i=0;i<note_length;++i){
        const float x=.18f*std::sin(2*PI*220*i/SR)+randnoise()*.012f;
        note[i*2]=note[i*2+1]=x;
    }
    run(note,note_out,2);
    const float energy_in=rms(note,1500,note_length,0,2);
    const float energy_out=rms(note_out,1500,note_length,0,2);
    const float reduction=20*std::log10(energy_out/energy_in);
    std::printf("Note RMS change: %.2f dB\n",reduction);
    check(reduction > -2.0f && reduction < 0.5f,"musical signal preserved");

    // Sharp bass-string-like attack: no clipped or swallowed onset.
    std::vector<float> transient(SR/2*2), transient_out;
    for(int i=0;i<SR/2;++i) {
        float v=randnoise()*.012f;
        if (i>=3000 && i<3050) v += .7f*std::exp(-(i-3000)/20.0f);
        transient[2*i]=transient[2*i+1]=v;
    }
    run(transient,transient_out,2);
    float onset=0;
    for(int i=3000+DELAY;i<3060+DELAY;++i)
        onset=std::max(onset,std::fabs(transient_out[2*i]));
    std::printf("Transient output peak: %.4f\n",onset);
    check(onset>.56f,"string attack transient preserved");

    // In-place must behave as out-of-place on same internal stream.
    plugin_init(SR,1,128);
    std::vector<float> signal(5000),out, inplace;
    for(int i=0;i<5000;++i)signal[i]=.22f*std::sin(2*PI*480*i/SR);
    run(signal,out,1,71);
    plugin_init(SR,1,128);
    inplace=signal;
    for(int f=0;f<5000;f+=71) {
        int n=std::min(71,5000-f);
        wyldfx_process(inplace.data()+f,inplace.data()+f,n,1);
    }
    float diff=0;
    for(int i=0;i<5000;++i)diff=std::max(diff,std::fabs(inplace[i]-out[i]));
    std::printf("In-place difference: %.8f\n",diff);
    check(diff<0.00001f,"in-place audio is safe");
    std::puts("All native denoiser tests passed.");
}
