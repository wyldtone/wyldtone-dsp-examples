// Wyldtone Smooth Downward Expander (v0.1.0)
//
// Fully in-place safe, stereo-linked, zero-lookahead expander. No allocation,
// locking or I/O in the processing path. The output remains unity above the
// soft knee; reduction increases continuously beneath the threshold and is
// capped at the user-selected maximum.
#include "../wyldtone_fx_common/Dsp.h"

using namespace wyldfx;

namespace {
constexpr ParameterSpec PARAMETERS[] = {
    {"threshold", "Threshold", "dB", -85.0f, -15.0f, -55.0f, 0.5f},
    {"ratio", "Ratio", "x", 1.0f, 8.0f, 2.5f, 0.1f},
    {"attack", "Attack", "ms", 1.0f, 40.0f, 5.0f, 0.5f},
    {"hold", "Hold", "ms", 0.0f, 250.0f, 65.0f, 1.0f},
    {"release", "Release", "ms", 50.0f, 1200.0f, 330.0f, 5.0f},
    {"knee", "Soft knee", "dB", 0.0f, 18.0f, 10.0f, 0.5f},
    {"max_reduction", "Max reduction", "dB", 0.0f, 60.0f, 24.0f, 1.0f},
    {"detector_hp", "Detector HP", "Hz", 15.0f, 180.0f, 25.0f, 1.0f},
};
constexpr int32_t PARAM_COUNT = sizeof(PARAMETERS) / sizeof(PARAMETERS[0]);

float sample_rate = 48000.0f;
float envelope = 0.0f;
float gain = 1.0f;
float hold_remaining = 0.0f;
OnePoleHP detector_hpf[2];

// The quantity under the threshold is dB of input level below threshold.
// At and above threshold + knee/2, there is no attenuation. With a soft
// knee, the first derivative of the gain curve is also continuous.
float reduction_db(float level_db, float threshold_db, float ratio,
                   float knee_db, float limit_db) {
    if (ratio <= 1.0f || limit_db <= 0.0f) return 0.0f;
    const float below = threshold_db - level_db;
    float attenuation = 0.0f;
    if (knee_db <= 0.0f) {
        attenuation = std::max(0.0f, below) * (ratio - 1.0f);
    } else {
        const float half = knee_db * 0.5f;
        if (below >= half) {
            attenuation = below * (ratio - 1.0f);
        } else if (below > -half) {
            const float x = below + half;
            attenuation = (ratio - 1.0f) * x * x / (2.0f * knee_db);
        }
    }
    return std::min(attenuation, limit_db);
}
}

extern "C" const char* wyldfx_name() { return "Smooth Downward Expander"; }
extern "C" const ParameterSpec* wyldfx_parameters() { return PARAMETERS; }
extern "C" int32_t wyldfx_parameter_count() { return PARAM_COUNT; }

extern "C" void wyldfx_prepare(int32_t rate, int32_t, int32_t) {
    sample_rate = static_cast<float>(rate > 0 ? rate : 48000);
    envelope = 0.0f;
    gain = 1.0f;
    hold_remaining = 0.0f;
    for (auto& filter : detector_hpf) filter.reset();
}

extern "C" void wyldfx_process(const float* input, float* output, int32_t frames,
                               int32_t channels) {
    if (!input || !output || frames <= 0 || (channels != 1 && channels != 2)) return;

    // Read host-visible parameter values once per block, never per sample.
    const float threshold = clamp(wyldfx_parameter(0), -85.0f, -15.0f);
    const float ratio = clamp(wyldfx_parameter(1), 1.0f, 8.0f);
    const float attack_ms = clamp(wyldfx_parameter(2), 1.0f, 40.0f);
    const float hold_samples = clamp(wyldfx_parameter(3), 0.0f, 250.0f)
                               * sample_rate / 1000.0f;
    const float release_ms = clamp(wyldfx_parameter(4), 50.0f, 1200.0f);
    const float knee_db = clamp(wyldfx_parameter(5), 0.0f, 18.0f);
    const float max_reduction = clamp(wyldfx_parameter(6), 0.0f, 60.0f);
    const float floor_gain = db_to_linear(-max_reduction);
    const float hp_hz = clamp(wyldfx_parameter(7), 15.0f, 180.0f);

    // Faster detection of new notes; longer release avoids chattering on
    // decaying bass notes. Gain opening also follows the attack parameter.
    const float detector_attack = smooth_alpha(attack_ms, sample_rate);
    const float detector_release = smooth_alpha(std::max(55.0f, release_ms * 0.45f), sample_rate);
    const float gain_attack = detector_attack;
    const float gain_release = smooth_alpha(release_ms, sample_rate);
    for (int ch = 0; ch < channels; ++ch) detector_hpf[ch].set(hp_hz, sample_rate);

    for (int frame = 0; frame < frames; ++frame) {
        float peak = 0.0f;
        for (int ch = 0; ch < channels; ++ch) {
            const int index = frame * channels + ch;
            const float sample = finite_sample(input[index]);
            peak = std::max(peak, std::fabs(detector_hpf[ch].run(sample)));
        }

        const float e_coeff = peak > envelope ? detector_attack : detector_release;
        envelope = peak + e_coeff * (envelope - peak);
        envelope = envelope < 1.0e-12f ? 0.0f : envelope;

        const float level_db = linear_to_db(envelope);
        // Hold starts only after an actual signal reaches the upper knee.
        // Don't continually reset it from the noise floor inside the knee.
        if (level_db >= threshold + knee_db * 0.5f) {
            hold_remaining = hold_samples;
        } else if (hold_remaining > 0.0f) {
            hold_remaining -= 1.0f;
        }

        const float attenuation = hold_remaining > 0.0f ? 0.0f :
            reduction_db(level_db, threshold, ratio, knee_db, max_reduction);
        const float target = db_to_linear(-attenuation);
        const float g_coeff = target > gain ? gain_attack : gain_release;
        gain = target + g_coeff * (gain - target);
        gain = clamp(gain, floor_gain, 1.0f);

        for (int ch = 0; ch < channels; ++ch) {
            const int index = frame * channels + ch;
            const float sample = finite_sample(input[index]);
            output[index] = finite_sample(sample * gain);
        }
    }
}
