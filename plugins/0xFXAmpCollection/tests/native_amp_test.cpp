// SPDX-License-Identifier: Apache-2.0
// Native test runs the same MIT 0xFX C99 source as the WAP build.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>

extern "C" {
void plugin_init(int32_t rate, int32_t channels, int32_t frames);
int32_t plugin_name(int32_t buffer, int32_t capacity);
int32_t plugin_buffer_ptr();
void plugin_process(int32_t buffer, int32_t sample_count);
int32_t plugin_parameter_count();
int32_t plugin_parameter_flags(int32_t index);
float plugin_parameter_get(int32_t index);
void plugin_parameter_set(int32_t index, float value);
int32_t plugin_parameter_choice(int32_t param, int32_t choice, int32_t dst, int32_t capacity);
int32_t plugin_pending_events();
float* wyldtone_amp_native_audio_buffer();
}

int main() {
    constexpr int frames = 512;
    constexpr int channels = 2;
    constexpr float pi = 3.14159265358979323846f;
    plugin_init(48000, channels, frames);
    assert(plugin_parameter_count() == 12);
    assert(plugin_parameter_flags(0) == (1 | 8)); // Choice/Automatable
    assert(plugin_parameter_flags(1) == 1); // Gain/Automatable
    float* samples = wyldtone_amp_native_audio_buffer();
    float baseline = 0.0f;
    for (int model = 0; model < 12; ++model) {
        plugin_parameter_set(0, static_cast<float>(model) / 11.0f);
        assert(std::abs(plugin_parameter_get(0) - model / 11.0f) < 1e-5f);
        float level = 0.0f;
        for (int pass = 0; pass < 4; ++pass) {
            for (int f = 0; f < frames; ++f) {
                const float tone = std::sin(2 * pi * 110.0f * (f + pass * frames) / 48000.0f);
                samples[f * channels] = tone * 0.05f;
                samples[f * channels + 1] = tone * 0.025f;
            }
            plugin_process(plugin_buffer_ptr(), frames * channels);
            for (int f = 0; f < frames * channels; ++f) {
                assert(std::isfinite(samples[f]));
                assert(std::abs(samples[f]) <= 8.0f);
                level += std::abs(samples[f]);
            }
        }
        assert(level > 1e-3f && "Amp model must produce non-silent output");
        if (model == 0) baseline = level;
        std::printf("model %d: sum %.4f\n", model, level);
    }
    assert(baseline > 0.0f);

    // Regression for upstream Sag bug: previously the envelope was compared
    // to the supply voltage, which could keep Sag permanently inactive.
    auto steadyLevel = [&](float sag) {
        plugin_init(48000, channels, frames);
        plugin_parameter_set(1, 0.85f); // Gain
        plugin_parameter_set(2, 0.80f); // Volume
        plugin_parameter_set(7, sag);   // Sag
        plugin_parameter_set(8, 0.70f); // Master
        double sum = 0.0;
        int measured = 0;
        for (int pass = 0; pass < 120; ++pass) {
            for (int frame = 0; frame < frames; ++frame) {
                const float x = 0.35f * std::sin(
                    2.0f * pi * 110.0f * (frame + pass * frames) / 48000.0f);
                samples[frame * 2] = x;
                samples[frame * 2 + 1] = x * 0.75f;
            }
            plugin_process(plugin_buffer_ptr(), frames * channels);
            if (pass >= 100) {
                for (int i = 0; i < frames * channels; ++i) {
                    assert(std::isfinite(samples[i]));
                    sum += std::abs(samples[i]);
                    ++measured;
                }
            }
        }
        return sum / measured;
    };
    const double noSag = steadyLevel(0.0f);
    const double fullSag = steadyLevel(1.0f);
    assert(noSag > 0.00001 && "Fullerton Clean must produce output");
    std::printf("Sag A/B level: %.7f vs %.7f\n", noSag, fullSag);
    assert(std::abs(noSag - fullSag) > noSag * 0.001 &&
           "Sag must measurably change a sustained, non-clipped signal");

    std::puts("0xFX Amp Collection: native all-model smoke test passed");
    return 0;
}
