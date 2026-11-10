#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace wyldfx {
struct ParameterSpec {
    const char* id;
    const char* name;
    const char* unit;
    float min;
    float max;
    float initial;
    float step;
};

inline float clamp(float x, float low, float high) {
    return std::isfinite(x) ? std::max(low, std::min(x, high)) : low;
}
inline float db_to_linear(float db) {
    return std::pow(10.0f, db / 20.0f);
}
inline float linear_to_db(float amp) {
    return 20.0f * std::log10(std::max(amp, 1e-8f));
}
inline float finite_sample(float x) {
    return std::isfinite(x) ? x : 0.0f;
}
inline float interp(const float* ring, int len, float pos) {
    while (pos < 0.0f) pos += static_cast<float>(len);
    while (pos >= len) pos -= static_cast<float>(len);
    const int a = static_cast<int>(pos);
    const int b = (a + 1 == len) ? 0 : a + 1;
    const float frac = pos - static_cast<float>(a);
    return ring[a] + frac * (ring[b] - ring[a]);
}
}

// Each effect provides these five symbols. FxShell.cpp owns the WASM ABI.
extern "C" const char* wyldfx_name();
extern "C" const wyldfx::ParameterSpec* wyldfx_parameters();
extern "C" int32_t wyldfx_parameter_count();
extern "C" void wyldfx_prepare(int32_t sample_rate, int32_t channels, int32_t block_size);
extern "C" void wyldfx_process(const float* in, float* out, int32_t frames, int32_t channels);
extern "C" float wyldfx_parameter(int32_t index);
