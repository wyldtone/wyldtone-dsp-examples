// Dedicated Wyldtone C++/WASM ABI bridge for the spectral denoiser.
// Compatible with the regular FxShell ABI, with an optional opaque state event.
// No changes to shared FxShell.cpp or the audio engine are required.
#include "../wyldtone_fx_common/FxShell.h"
#include <atomic>
#include <algorithm>
#include <cstring>
#include <cstdint>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define OPTIONAL_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define OPTIONAL_EXPORT
#endif

namespace {
constexpr int32_t CAPACITY = 16384;
constexpr int32_t MAX_PARAMS = 32;
float buffer[CAPACITY] = {};
float values[MAX_PARAMS] = {};
std::atomic<uint8_t> pending{0};
int32_t channels = 2;

int32_t copy_text(const char* str, int32_t pointer, int32_t capacity) {
    if (capacity <= 0 || pointer <= 0 || !str) return 0;
    const int32_t length = static_cast<int32_t>(std::strlen(str));
    const int32_t count = std::min(length, capacity - 1);
    auto* dest = reinterpret_cast<char*>(static_cast<uintptr_t>(pointer));
    std::memcpy(dest, str, static_cast<size_t>(count));
    dest[count] = '\0';
    return count;
}
const wyldfx::ParameterSpec* spec(int32_t i) {
    return (i < 0 || i >= wyldfx_parameter_count() || i >= MAX_PARAMS)
        ? nullptr : &wyldfx_parameters()[i];
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
    const auto* p = spec(i); return p ? copy_text(p->id, ptr, size) : 0;
}
int32_t plugin_parameter_name(int32_t i, int32_t ptr, int32_t size) {
    const auto* p = spec(i); return p ? copy_text(p->name, ptr, size) : 0;
}
int32_t plugin_parameter_label(int32_t i, int32_t ptr, int32_t size) {
    const auto* p = spec(i); return p ? copy_text(p->unit, ptr, size) : 0;
}
float plugin_parameter_min(int32_t i) { const auto* p=spec(i); return p ? p->min : 0; }
float plugin_parameter_max(int32_t i) { const auto* p=spec(i); return p ? p->max : 0; }
float plugin_parameter_default(int32_t i) { const auto* p=spec(i); return p ? p->initial : 0; }
float plugin_parameter_step(int32_t i) { const auto* p=spec(i); return p ? p->step : 0; }
// Learn is a non-automatable Boolean control. The other controls are ordinary floats.
int32_t plugin_parameter_flags(int32_t i) { return spec(i) ? (i == 0 ? 0x04 : 0x01) : 0; }
float plugin_parameter_get(int32_t i) {
    const auto* p=spec(i);
    return p && p->max>p->min ?
        wyldfx::clamp((values[i]-p->min)/(p->max-p->min),0,1):0;
}
void plugin_parameter_set(int32_t i, float normalized) {
    const auto* p=spec(i);
    if(!p) return;
    const float v=p->min+(p->max-p->min)*wyldfx::clamp(normalized,0,1);
    if(values[i]!=v) { values[i]=v; pending.fetch_or(0x01, std::memory_order_release); }
}
float wyldfx_parameter(int32_t i) { return spec(i) ? values[i] : 0; }
void wyldfx_notify_events(uint8_t mask) {
    pending.fetch_or(mask, std::memory_order_release);
}
void wyldfx_write_parameter(int32_t i, float value) {
    const auto* p = spec(i);
    if (!p) return;
    const float v = wyldfx::clamp(value, p->min, p->max);
    if (values[i] != v) { values[i] = v; pending.fetch_or(0x01, std::memory_order_release); }
}
int32_t plugin_buffer_ptr() { return static_cast<int32_t>(reinterpret_cast<uintptr_t>(buffer)); }
void plugin_process(int32_t ptr, int32_t sample_count) {
    if (ptr!=plugin_buffer_ptr() || sample_count<0 || sample_count>CAPACITY ||
        channels<1 || sample_count%channels!=0) return;
    wyldfx_process(buffer, buffer, sample_count/channels, channels);
}
OPTIONAL_EXPORT int32_t plugin_input_bus_count() { return 1; }
OPTIONAL_EXPORT int32_t plugin_output_bus_count() { return 1; }
OPTIONAL_EXPORT int32_t plugin_input_bus_channels(int32_t bus) { return bus==0?2:0; }
OPTIONAL_EXPORT int32_t plugin_output_bus_channels(int32_t bus) { return bus==0?2:0; }
OPTIONAL_EXPORT void plugin_process_buses(int32_t ptr, int32_t frames) {
    if(ptr!=plugin_buffer_ptr() || frames<0 || static_cast<int64_t>(frames)*4>CAPACITY) return;
    wyldfx_process(buffer, buffer+frames*2, frames, 2);
}
uint8_t plugin_pending_events() {return pending.exchange(0,std::memory_order_acquire);}
}
