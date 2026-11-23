// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Wyldtone contributors
// Wyldtone WAP adapter for the MIT-licensed 0xFX amplifier DSP by Dan Michael.
// See licenses/Wyldtone-Apache-2.0.txt and licenses/0xFX-MIT.txt.
// Based on https://github.com/averagenative/0xFX at 5b248779c96dce6e64d0ed29cfa598014dc4d3c4.
#include "wasm_plugin.h"
#include "vendor/0xfx/engine_internal.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {
constexpr int kMaxAudioFloats = 16384;
constexpr int kChunkFrames = 512;
constexpr int kFadeFrames = 128;
constexpr int kModelCount = FX_AMP_COUNT;
static_assert(kModelCount == 12, "Update choice labels if upstream models change");

struct Spec {
    const char* id;
    const char* name;
    const char* unit;
    fx_amp_param_t upstream;
    float initial;
};
constexpr Spec kParameters[] = {
    {"model",    "Amp Model", "",   FX_AMP_PARAM_COUNT,   0.0f},
    {"gain",     "Gain",      "",   FX_AMP_PARAM_GAIN,    0.5f},
    {"volume",   "Volume",    "",   FX_AMP_PARAM_VOLUME,  0.5f},
    {"bass",     "Bass",      "",   FX_AMP_PARAM_BASS,    0.5f},
    {"mid",      "Mid",       "",   FX_AMP_PARAM_MID,     0.5f},
    {"treble",   "Treble",    "",   FX_AMP_PARAM_TREBLE,  0.5f},
    {"presence", "Presence",  "",   FX_AMP_PARAM_PRESENCE,0.5f},
    {"sag",      "Sag",       "",   FX_AMP_PARAM_SAG,     0.3f},
    {"master",   "Master",    "",   FX_AMP_PARAM_MASTER,  0.5f},
    {"cut",      "Cut",       "",   FX_AMP_PARAM_CUT,     0.0f},
    {"tone",     "Tone",      "",   FX_AMP_PARAM_TONE,    0.5f},
    {"feedback", "Feedback",  "",   FX_AMP_PARAM_FEEDBACK,0.0f}
};
constexpr int kParameterCount = sizeof(kParameters) / sizeof(kParameters[0]);

float audio[kMaxAudioFloats] = {};
float block[2][kChunkFrames] = {};
float previousBlock[2][kChunkFrames] = {};
fx_amp_state_t amps[2] = {};
fx_amp_state_t oldAmps[2] = {};
float values[kParameterCount] = {};
int sampleRate = 48000;
int inputChannels = 2;
int currentModel = 0;
int fadeFramesLeft = 0;
bool initialized = false;
std::atomic<int32_t> pending{0};

int32_t copyText(const char* string, int32_t dest, int32_t capacity) {
    if (!string || capacity <= 0 || dest <= 0) return 0;
    const size_t length = std::strlen(string);
    const int32_t count = static_cast<int32_t>(
        std::min(length, static_cast<size_t>(capacity - 1)));
    char* output = reinterpret_cast<char*>(static_cast<uintptr_t>(dest));
    std::memcpy(output, string, static_cast<size_t>(count));
    output[count] = '\0';
    return count;
}
bool validParam(int32_t index) {
    return index >= 0 && index < kParameterCount;
}
float clamp01(float x) {
    return std::isfinite(x) ? std::max(0.0f, std::min(1.0f, x)) : 0.0f;
}
int selectedModel() {
    return std::max(0, std::min(kModelCount - 1,
        static_cast<int>(std::lround(values[0] * (kModelCount - 1)))));
}
void updateParameters() {
    const int selected = selectedModel();
    if (selected != currentModel) {
        // Keep the previous model running during a short no-click crossfade.
        for (int channel = 0; channel < 2; ++channel) {
            oldAmps[channel] = amps[channel];
            fx_amp_init(&amps[channel], static_cast<fx_amp_type_t>(selected));
        }
        currentModel = selected;
        fadeFramesLeft = kFadeFrames;
    }
    for (int channel = 0; channel < 2; ++channel) {
        for (int i = 1; i < kParameterCount; ++i) {
            amps[channel].params[kParameters[i].upstream] = values[i];
        }
    }
}
void processInterleaved(const float* input, float* output, int32_t frames, int channels) {
    if (!initialized || !input || !output || frames <= 0 ||
        (channels != 1 && channels != 2)) return;
    updateParameters();
    int offset = 0;
    while (offset < frames) {
        const int chunk = std::min(kChunkFrames, frames - offset);
        const bool fading = fadeFramesLeft > 0;
        for (int c = 0; c < channels; ++c) {
            for (int f = 0; f < chunk; ++f) {
                const float x = input[(offset + f) * channels + c];
                const float safe = std::isfinite(x) ? x : 0.0f;
                block[c][f] = safe;
                if (fading) previousBlock[c][f] = safe;
            }
            fx_amp_process(&amps[c], block[c], chunk, static_cast<float>(sampleRate));
            if (fading) {
                fx_amp_process(&oldAmps[c], previousBlock[c], chunk,
                               static_cast<float>(sampleRate));
            }
        }
        for (int f = 0; f < chunk; ++f) {
            const float mix = fadeFramesLeft > 0
                ? 1.0f - static_cast<float>(fadeFramesLeft) / kFadeFrames : 1.0f;
            for (int c = 0; c < channels; ++c) {
                const float wet = block[c][f];
                const float previous = fading ? previousBlock[c][f] : wet;
                const float sum = previous * (1.0f - mix) + wet * mix;
                output[(offset + f) * channels + c] =
                    std::isfinite(sum) ? std::max(-8.0f, std::min(8.0f, sum)) : 0.0f;
            }
            if (fadeFramesLeft > 0) --fadeFramesLeft;
        }
        offset += chunk;
    }
}
} // namespace

extern "C" {
void plugin_init(int32_t rate, int32_t channels, int32_t /*blockFrames*/) {
    sampleRate = rate >= 8000 && rate <= 384000 ? rate : 48000;
    inputChannels = channels == 1 ? 1 : 2;
    for (int i = 0; i < kParameterCount; ++i) values[i] = kParameters[i].initial;
    currentModel = selectedModel();
    for (auto& amp : amps) fx_amp_init(&amp, static_cast<fx_amp_type_t>(currentModel));
    fadeFramesLeft = 0;
    initialized = true;
    pending.store(0, std::memory_order_relaxed);
}
int32_t plugin_name(int32_t dest, int32_t size) {
    return copyText("0xFX Amp Collection", dest, size);
}
int32_t plugin_buffer_ptr() {
    return static_cast<int32_t>(reinterpret_cast<uintptr_t>(audio));
}
void plugin_process(int32_t ptr, int32_t count) {
    if (ptr != plugin_buffer_ptr() || count < 0 || count > kMaxAudioFloats ||
        inputChannels < 1 || count % inputChannels != 0) return;
    processInterleaved(audio, audio, count / inputChannels, inputChannels);
}
WASM_PLUGIN_EXPORT int32_t plugin_input_bus_count() { return 1; }
WASM_PLUGIN_EXPORT int32_t plugin_output_bus_count() { return 1; }
WASM_PLUGIN_EXPORT int32_t plugin_input_bus_channels(int32_t bus) { return bus == 0 ? 2 : 0; }
WASM_PLUGIN_EXPORT int32_t plugin_output_bus_channels(int32_t bus) { return bus == 0 ? 2 : 0; }
WASM_PLUGIN_EXPORT void plugin_process_buses(int32_t ptr, int32_t frames) {
    if (ptr != plugin_buffer_ptr() || frames < 0 ||
        static_cast<int64_t>(frames) * 4 > kMaxAudioFloats) return;
    processInterleaved(audio, audio + frames * 2, frames, 2);
}
int32_t plugin_parameter_count() { return kParameterCount; }
int32_t plugin_parameter_id(int32_t index, int32_t dest, int32_t cap) {
    return validParam(index) ? copyText(kParameters[index].id, dest, cap) : 0;
}
int32_t plugin_parameter_name(int32_t index, int32_t dest, int32_t cap) {
    return validParam(index) ? copyText(kParameters[index].name, dest, cap) : 0;
}
int32_t plugin_parameter_label(int32_t index, int32_t dest, int32_t cap) {
    return validParam(index) ? copyText(kParameters[index].unit, dest, cap) : 0;
}
float plugin_parameter_min(int32_t /*index*/) { return 0.0f; }
float plugin_parameter_max(int32_t index) { return index == 0 ? 11.0f : 1.0f; }
float plugin_parameter_default(int32_t index) {
    return validParam(index) ? kParameters[index].initial * (index == 0 ? 11.0f : 1.0f) : 0.0f;
}
float plugin_parameter_step(int32_t index) { return index == 0 ? 1.0f : 0.01f; }
int32_t plugin_parameter_flags(int32_t index) {
    if (!validParam(index)) return 0;
    return index == 0 ? (1 | 8) : 1; // automatable + Choice for model
}
WASM_PLUGIN_EXPORT int32_t plugin_parameter_choice(
    int32_t index, int32_t choice, int32_t dest, int32_t capacity) {
    if (index != 0 || choice < 0 || choice >= kModelCount) return 0;
    return copyText(fx_amp_get_type_name(static_cast<fx_amp_type_t>(choice)), dest, capacity);
}
float plugin_parameter_get(int32_t index) {
    return validParam(index) ? values[index] : 0.0f;
}
void plugin_parameter_set(int32_t index, float normalized) {
    if (!validParam(index)) return;
    const float safe = clamp01(normalized);
    const float value = index == 0
        ? std::round(safe * (kModelCount - 1)) / (kModelCount - 1) : safe;
    if (value != values[index]) {
        values[index] = value;
        pending.fetch_or(1, std::memory_order_release);
    }
}
int32_t plugin_pending_events() {
    return pending.exchange(0, std::memory_order_acquire);
}
} // extern "C"
