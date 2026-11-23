// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Wyldtone contributors
// Wyldtone WAP adapter for the MIT-licensed 0xFX amplifier DSP by Dan Michael.
// See licenses/Wyldtone-Apache-2.0.txt and licenses/0xFX-MIT.txt.
// Based on https://github.com/averagenative/0xFX at 5b248779c96dce6e64d0ed29cfa598014dc4d3c4.
#include "wasm_plugin.h"
#include "vendor/0xfx/engine_internal.h"
#include "oversampling.h"
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
    {"feedback", "Feedback",  "",   FX_AMP_PARAM_FEEDBACK,0.0f},
    // Append only: old parameter IDs and indexes must remain stable for presets.
    {"oversampling", "Oversampling", "", FX_AMP_PARAM_COUNT, 0.0f}
};
constexpr int kParameterCount = sizeof(kParameters) / sizeof(kParameters[0]);
constexpr int kOversamplingIndex = 12;
constexpr int kMaxFactor = 4;

float audio[kMaxAudioFloats] = {};
float block[2][kChunkFrames] = {};
float previousBlock[2][kChunkFrames] = {};
float expanded[kChunkFrames * kMaxFactor] = {};
float intermediate[kChunkFrames * 2] = {};
amp_os::State oversamplers[2] = {};
amp_os::State oldOversamplers[2] = {};
fx_amp_state_t amps[2] = {};
fx_amp_state_t oldAmps[2] = {};
float values[kParameterCount] = {};
int sampleRate = 48000;
int inputChannels = 2;
int currentModel = 0;
int currentFactor = 1;
int oldFactor = 1;
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
int selectedFactor() {
    const int step = std::max(0, std::min(2,
        static_cast<int>(std::lround(values[kOversamplingIndex] * 2.0f))));
    return step == 0 ? 1 : step == 1 ? 2 : 4;
}
// Each channel has separate resampler state; processing is block-continuous.
// FIR filters are applied before and after ALL nonlinear gain stages.
// Process one channel at a time to reuse fixed scratch storage.
void processAmp(fx_amp_state_t& amp, amp_os::State& os,
                float* samples, int frames, int factor) {
    if (factor == 1) {
        fx_amp_process(&amp, samples, frames, static_cast<float>(sampleRate));
        return;
    }
    if (factor == 2) {
        for (int i = 0; i < frames; ++i) {
            os.first.up(samples[i], expanded[i * 2], expanded[i * 2 + 1]);
        }
        fx_amp_process(&amp, expanded, frames * 2, static_cast<float>(sampleRate * 2));
        for (int i = 0; i < frames; ++i) {
            samples[i] = os.first.down(expanded[i * 2], expanded[i * 2 + 1]);
        }
        return;
    }
    for (int i = 0; i < frames; ++i) {
        os.first.up(samples[i], intermediate[i * 2], intermediate[i * 2 + 1]);
    }
    for (int i = 0; i < frames * 2; ++i) {
        os.second.up(intermediate[i], expanded[i * 2], expanded[i * 2 + 1]);
    }
    fx_amp_process(&amp, expanded, frames * 4, static_cast<float>(sampleRate * 4));
    for (int i = 0; i < frames * 2; ++i) {
        intermediate[i] = os.second.down(expanded[i * 2], expanded[i * 2 + 1]);
    }
    for (int i = 0; i < frames; ++i) {
        samples[i] = os.first.down(intermediate[i * 2], intermediate[i * 2 + 1]);
    }
}
void updateParameters() {
    const int selected = selectedModel();
    const int selectedOs = selectedFactor();
    if (selected != currentModel || selectedOs != currentFactor) {
        // A full state reset is required when changing DSP sample rates.
        // Keep the old model AND old resampler alive for a short crossfade.
        for (int channel = 0; channel < 2; ++channel) {
            oldAmps[channel] = amps[channel];
            oldOversamplers[channel] = oversamplers[channel];
            fx_amp_init(&amps[channel], static_cast<fx_amp_type_t>(selected));
            oversamplers[channel] = amp_os::State{};
        }
        oldFactor = currentFactor;
        currentFactor = selectedOs;
        currentModel = selected;
        fadeFramesLeft = kFadeFrames;
    }
    for (int channel = 0; channel < 2; ++channel) {
        for (int i = 1; i < kOversamplingIndex; ++i) {
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
            processAmp(amps[c], oversamplers[c], block[c], chunk, currentFactor);
            if (fading) {
                processAmp(oldAmps[c], oldOversamplers[c], previousBlock[c], chunk, oldFactor);
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
    currentFactor = selectedFactor();
    oldFactor = currentFactor;
    for (int c = 0; c < 2; ++c) {
        fx_amp_init(&amps[c], static_cast<fx_amp_type_t>(currentModel));
        oversamplers[c] = amp_os::State{};
        oldOversamplers[c] = amp_os::State{};
    }
    fadeFramesLeft = 0;
    initialized = true;
    pending.store(0, std::memory_order_relaxed);
}
int32_t plugin_name(int32_t dest, int32_t size) {
    return copyText("Amp Collection", dest, size);
}
int32_t plugin_buffer_ptr() {
    return static_cast<int32_t>(reinterpret_cast<uintptr_t>(audio));
}
void plugin_process(int32_t ptr, int32_t count) {
    if (ptr != plugin_buffer_ptr() || count < 0 || count > kMaxAudioFloats ||
        inputChannels < 1 || count % inputChannels != 0) return;
    processInterleaved(audio, audio, count / inputChannels, inputChannels);
}
// Current and worst-case algorithmic latency in HOST sample frames.
// The value can change via the Oversampling Choice; the host reserves the
// worst-case 46 samples during graph prepare.
WASM_PLUGIN_EXPORT int32_t plugin_latency_samples() {
    return amp_os::latencySamples(selectedFactor());
}
WASM_PLUGIN_EXPORT int32_t plugin_max_latency_samples() {
    return amp_os::latencySamples(4);
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
float plugin_parameter_max(int32_t index) { return index == 0 ? 11.0f : index == kOversamplingIndex ? 2.0f : 1.0f; }
float plugin_parameter_default(int32_t index) {
    return validParam(index) ? kParameters[index].initial * (index == 0 ? 11.0f : index == kOversamplingIndex ? 2.0f : 1.0f) : 0.0f;
}
float plugin_parameter_step(int32_t index) { return (index == 0 || index == kOversamplingIndex) ? 1.0f : 0.01f; }
int32_t plugin_parameter_flags(int32_t index) {
    if (!validParam(index)) return 0;
    return (index == 0 || index == kOversamplingIndex) ? (1 | 8) : 1; // Choice / automatable
}
WASM_PLUGIN_EXPORT int32_t plugin_parameter_choice(
    int32_t index, int32_t choice, int32_t dest, int32_t capacity) {
    if (index == kOversamplingIndex && choice >= 0 && choice <= 2) {
        static const char* labels[] = {"Off (1x)", "2x", "4x"};
        return copyText(labels[choice], dest, capacity);
    }
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
        ? std::round(safe * (kModelCount - 1)) / (kModelCount - 1)
        : index == kOversamplingIndex ? std::round(safe * 2.0f) / 2.0f : safe;
    if (value != values[index]) {
        values[index] = value;
        pending.fetch_or(index == kOversamplingIndex ? (1 | 8) : 1,
                         std::memory_order_release);
    }
}
int32_t plugin_pending_events() {
    return pending.exchange(0, std::memory_order_acquire);
}
#ifdef WYLDTONE_NATIVE_TEST
// Exposed only for host-native regression tests; not compiled into the WAP.
float* wyldtone_amp_native_audio_buffer() { return audio; }
#endif
} // extern "C"
