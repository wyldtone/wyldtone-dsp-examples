// Shared, allocation-free in-process bridge for Wyldtone C++/WASM effects.
#include "FxShell.h"
#include "wasm_plugin.h"
#include <atomic>

namespace {
constexpr int32_t CAPACITY = 16384;
constexpr int32_t MAX_PARAMS = 32;
static float buffer[CAPACITY] = {};
static float values[MAX_PARAMS] = {};
static std::atomic<uint8_t> pending{0};
static int32_t channels = 2;

int32_t copy_text(const char* str, int32_t pointer, int32_t capacity) {
    if (capacity <= 0 || pointer <= 0 || str == nullptr) return 0;
    const int32_t length = static_cast<int32_t>(std::strlen(str));
    const int32_t count = std::min(length, capacity - 1);
    char* dest = reinterpret_cast<char*>(static_cast<uintptr_t>(pointer));
    std::memcpy(dest, str, static_cast<size_t>(count));
    dest[count] = '\0';
    return count;
}
const wyldfx::ParameterSpec* spec(int32_t i) {
    if (i < 0 || i >= wyldfx_parameter_count() || i >= MAX_PARAMS) return nullptr;
    return &wyldfx_parameters()[i];
}
}

extern "C" {
void plugin_init(int32_t rate, int32_t channel_count, int32_t block_size) {
    channels = channel_count == 1 ? 1 : 2;
    const int count = std::min(wyldfx_parameter_count(), MAX_PARAMS);
    for (int i = 0; i < count; ++i) values[i] = wyldfx_parameters()[i].initial;
    pending.store(0, std::memory_order_relaxed);
    wyldfx_prepare(rate, channels, block_size);
}
int32_t plugin_name(int32_t ptr, int32_t size) {
    return copy_text(wyldfx_name(), ptr, size);
}
int32_t plugin_parameter_count() { return wyldfx_parameter_count(); }
int32_t plugin_parameter_id(int32_t i, int32_t ptr, int32_t size) {
    const auto* p = spec(i);
    return p ? copy_text(p->id, ptr, size) : 0;
}
int32_t plugin_parameter_name(int32_t i, int32_t ptr, int32_t size) {
    const auto* p = spec(i);
    return p ? copy_text(p->name, ptr, size) : 0;
}
int32_t plugin_parameter_label(int32_t i, int32_t ptr, int32_t size) {
    const auto* p = spec(i);
    return p ? copy_text(p->unit, ptr, size) : 0;
}
float plugin_parameter_min(int32_t i) { const auto* p=spec(i); return p?p->min:0; }
float plugin_parameter_max(int32_t i) { const auto* p=spec(i); return p?p->max:0; }
float plugin_parameter_default(int32_t i) { const auto* p=spec(i); return p?p->initial:0; }
float plugin_parameter_step(int32_t i) { const auto* p=spec(i); return p?p->step:0; }
int32_t plugin_parameter_flags(int32_t i) { return spec(i)?0x01:0; }
float plugin_parameter_get(int32_t i) {
    const auto* p=spec(i);
    return p && p->max>p->min ?
        wyldfx::clamp((values[i]-p->min)/(p->max-p->min),0,1):0;
}
void plugin_parameter_set(int32_t i, float normalized) {
    const auto* p=spec(i);
    if(!p) return;
    const float v=p->min+(p->max-p->min)*wyldfx::clamp(normalized,0,1);
    if(values[i]!=v) {
        values[i]=v;
        pending.fetch_or(0x01, std::memory_order_release);
    }
}
float wyldfx_parameter(int32_t i) {
    return spec(i) ? values[i] : 0.0f;
}
int32_t plugin_buffer_ptr() {
    return static_cast<int32_t>(reinterpret_cast<uintptr_t>(buffer));
}
// All five effects use the stereo v2 bus ABI; legacy is retained for compatibility.
void plugin_process(int32_t ptr, int32_t sample_count) {
    if(ptr!=plugin_buffer_ptr() || sample_count<0 || sample_count>CAPACITY ||
       channels<1 || sample_count%channels!=0) return;
    wyldfx_process(buffer, buffer, sample_count/channels, channels);
}
WASM_PLUGIN_EXPORT int32_t plugin_input_bus_count() { return 1; }
WASM_PLUGIN_EXPORT int32_t plugin_output_bus_count() { return 1; }
WASM_PLUGIN_EXPORT int32_t plugin_input_bus_channels(int32_t bus) { return bus==0?2:0; }
WASM_PLUGIN_EXPORT int32_t plugin_output_bus_channels(int32_t bus) { return bus==0?2:0; }
WASM_PLUGIN_EXPORT void plugin_process_buses(int32_t ptr, int32_t frames) {
    if(ptr!=plugin_buffer_ptr() || frames<0 || static_cast<int64_t>(frames)*4>CAPACITY) return;
    const float* input=buffer;
    float* output=buffer+frames*2;
    wyldfx_process(input, output, frames, 2);
}
uint8_t plugin_pending_events() {return pending.exchange(0,std::memory_order_acquire);}

#ifdef WYLDFX_NATIVE_TEST
float* wyldfx_native_buffer() { return buffer; }
void wyldfx_native_process(float* input,float* output,int32_t frames,int32_t chans) {
    wyldfx_process(input,output,frames,chans);
}
#endif
}
