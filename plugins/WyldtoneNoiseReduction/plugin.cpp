// Wyldtone Noise Reduction v0.2.0
// Fixed-allocation 512-point, 4x overlapped STFT spectral denoiser.
// NOTE: Constant algorithmic latency of FFT_SIZE-1 samples (511 samples).
// The processing callback performs no allocations, filesystem I/O or locks.
#include "../wyldtone_fx_common/Dsp.h"
#include <array>
#include <complex>
#include <cstdint>
#include <cstring>
#include <cmath>

using namespace wyldfx;

// Implemented in the plugin-local bridge (DenoiserBridge.cpp).
extern "C" void wyldfx_notify_events(uint8_t mask);
extern "C" void wyldfx_write_parameter(int32_t index, float value);
extern "C" float wyldfx_parameter(int32_t index);

namespace {
constexpr int FFT_SIZE = 512;
constexpr int HOP_SIZE = 128;
constexpr int BINS = FFT_SIZE / 2 + 1;
constexpr float TWO_PI = 6.2831853071795864769f;
constexpr float SYNTHESIS_NORM = 1.5f; // Sum of squared periodic Hann windows, 75% overlap.
constexpr uint32_t STATE_MAGIC = 0x31524457; // "WDR1" in little-endian.

constexpr ParameterSpec PARAMETERS[] = {
    {"learn", "Learn noise", "", 0.0f, 1.0f, 0.0f, 1.0f},
    {"strength", "Reduction", "%", 0.0f, 100.0f, 45.0f, 1.0f},
    {"max_reduction", "Max reduction", "dB", 0.0f, 30.0f, 18.0f, 0.5f},
    {"preserve", "Protect transients", "%", 0.0f, 100.0f, 80.0f, 1.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f, 1.0f},
};
constexpr int PARAM_COUNT = sizeof(PARAMETERS) / sizeof(PARAMETERS[0]);

struct ProfileState {
    uint32_t magic;
    uint32_t bins;
    uint32_t valid;
    uint32_t sample_rate;
    float noise_power[BINS];
};
static_assert(sizeof(ProfileState) == 16 + BINS * sizeof(float), "Unexpected state alignment");

class Denoiser {
public:
    float sample_rate = 48000.0f;
    uint32_t profile_rate = 0;
    std::array<float, FFT_SIZE> window{};
    std::array<std::complex<float>, FFT_SIZE / 2> twiddle{};
    std::array<uint16_t, FFT_SIZE> bit_reverse{};
    std::array<std::array<float, FFT_SIZE>, 2> input_ring{};
    std::array<std::array<float, FFT_SIZE>, 2> output_ring{};
    std::array<std::array<std::complex<float>, FFT_SIZE>, 2> spectra{};
    std::array<float, BINS> noise_power{};
    std::array<float, BINS> learn_accum{};
    std::array<float, BINS> frame_power{};
    std::array<float, BINS> prev_power{};
    std::array<float, BINS> prev_signal_power{};
    std::array<float, BINS> prior_snr{};
    std::array<float, BINS> raw_gain{};
    std::array<float, BINS> frequency_gain{};
    std::array<float, BINS> smoothed_gain{};
    uint64_t processed_frames = 0;
    int learn_frames = 0;
    int learn_target = 450;
    int channel_count = 2;
    bool learning = false;
    bool profile_ready = false;
    bool learn_was_high = false;

    void prepare(int rate, int channels) {
        sample_rate = static_cast<float>(rate > 0 ? rate : 48000);
        channel_count = channels == 1 ? 1 : 2;
        if (profile_ready && profile_rate != static_cast<uint32_t>(sample_rate)) profile_ready = false;
        processed_frames = 0;
        for (auto& channel : input_ring) channel.fill(0);
        for (auto& channel : output_ring) channel.fill(0);
        for (auto& channel : spectra) channel.fill({0, 0});
        frame_power.fill(0);
        prev_power.fill(0);
        prev_signal_power.fill(0);
        prior_snr.fill(0);
        smoothed_gain.fill(1);
        raw_gain.fill(1);
        frequency_gain.fill(1);
        learning = false;
        learn_frames = 0;
        learn_was_high = false;
        learn_target = std::max(4, static_cast<int>(std::ceil(sample_rate * 2.5f / HOP_SIZE)));
        // Preserve the captured noise profile if the host resets the audio stream.
        for (int i = 0; i < FFT_SIZE; ++i) {
            window[i] = 0.5f * (1.0f - std::cos(TWO_PI * i / FFT_SIZE));
            uint16_t value = static_cast<uint16_t>(i), rev = 0;
            for (int bit = 0; bit <9; ++bit) { rev = static_cast<uint16_t>((rev << 1) | (value & 1)); value >>= 1; }
            bit_reverse[i] = rev;
        }
        for (int i = 0; i < FFT_SIZE/2; ++i) {
            const float angle = -TWO_PI * i / FFT_SIZE;
            twiddle[i] = {std::cos(angle), std::sin(angle)};
        }
    }

    void fft(std::array<std::complex<float>, FFT_SIZE>& buf, bool inverse) const {
        for (int i = 0; i < FFT_SIZE; ++i) {
            const int j = bit_reverse[i];
            if (j > i) std::swap(buf[i], buf[j]);
        }
        for (int length = 2; length <= FFT_SIZE; length <<= 1) {
            const int half = length >> 1;
            const int step = FFT_SIZE / length;
            for (int base = 0; base < FFT_SIZE; base += length) {
                for (int j = 0; j < half; ++j) {
                    const auto w = inverse ? std::conj(twiddle[j*step]) : twiddle[j*step];
                    const auto a = buf[base+j];
                    const auto b = buf[base+j+half] * w;
                    buf[base+j] = a + b;
                    buf[base+j+half] = a - b;
                }
            }
        }
        if (inverse) for (auto& bin : buf) bin /= static_cast<float>(FFT_SIZE);
    }

    void start_learning() {
        learning = true;
        learn_frames = 0;
        learn_accum.fill(0);
        // While learning, the plugin outputs its unmodified (but delayed) input.
        smoothed_gain.fill(1);
        prior_snr.fill(0);
        prev_signal_power.fill(0);
    }

    void process_fft_frame(int channels, float strength, float max_reduction_db, float protect) {
        const int ring_pos = static_cast<int>(processed_frames - 1) & (FFT_SIZE - 1);
        const int first = (ring_pos + 1) & (FFT_SIZE - 1);
        for (int ch = 0; ch < channels; ++ch) {
            for (int i = 0; i < FFT_SIZE; ++i) {
                const int ring_index = (first+i) & (FFT_SIZE-1);
                spectra[ch][i] = {input_ring[ch][ring_index] * window[i], 0.0f};
            }
            fft(spectra[ch], false);
        }
        for (int k = 0; k < BINS; ++k) {
            float p = 0;
            for (int ch = 0; ch < channels; ++ch) p += std::norm(spectra[ch][k]);
            frame_power[k] = p / static_cast<float>(channels);
        }

        if (learning) {
            for (int k=0; k<BINS; ++k) learn_accum[k] += frame_power[k];
            ++learn_frames;
            if (learn_frames >= learn_target) {
                float average_power = 0;
                for (int k=0; k<BINS; ++k) average_power += learn_accum[k];
                // Reject a profile learned from digital silence. Preserve prior valid profile.
                if (average_power > 1.0e-12f * static_cast<float>(learn_target)) {
                    for (int k=0; k<BINS; ++k)
                        noise_power[k] = std::max(learn_accum[k] / learn_frames, 1.0e-20f);
                    profile_ready = true;
                    profile_rate = static_cast<uint32_t>(sample_rate);
                    wyldfx_notify_events(0x02); // Host marks opaque state dirty.
                }
                learning = false;
                // Return the Learn control to Off when capture completes.
                // This is also a parameter-change notification to all UIs.
                wyldfx_write_parameter(0, 0.0f);
            }
        }

        const bool enabled = profile_ready && !learning && strength > 0.0001f && max_reduction_db > 0.0f;
        if (enabled) {
            // v32: Reduction controls the *effective* attenuation in decibels.
            // The old linear dry-mix limited 45% / 12 dB to only ~3.6 dB.
            // The square-root curve allows meaningful reductions at normal settings;
            // 0% is an exact neutral path, and 100% reaches Max reduction.
            const float effective_db = max_reduction_db * std::sqrt(strength);
            const float floor_gain = db_to_linear(-effective_db);
            const float overestimation = 1.1f + 0.9f * strength;

            // Decision-directed Wiener estimation: previous *clean* power guides
            // the current estimate, avoiding independently flickering FFT bins.
            // Use a smoothly learned local noise model (does not track music).
            for (int k = 0; k < BINS; ++k) {
                const float center_noise = noise_power[k];
                const float n0 = noise_power[k ? k-1 : k];
                const float n1 = noise_power[k+1 < BINS ? k+1 : k];
                const float noise = std::max(0.5f*center_noise + 0.25f*(n0+n1), 1.0e-20f);
                const float p = frame_power[k];
                const float posterior = std::min(10000.0f, p / (noise * overestimation));
                const float instant = std::max(0.0f, posterior - 1.0f);
                // A slow prior for the noise floor, but a prompt rise for a note.
                // Store CLEAN spectral power, not the already attenuated output.
                const float previous_clean = prev_signal_power[k] / (noise * overestimation);
                const float xi = std::max(0.0f, 0.91f*previous_clean + 0.09f*instant);
                prior_snr[k] = xi;
                const float wiener = xi / (xi + 1.0f);
                float target = clamp(wiener, floor_gain, 1.0f);

                // Protect fast spectral onsets. Do not mistake noise fluctuations
                // for a transient: require an absolute advantage over the noise.
                const float previous = prev_power[k];
                if (p > noise * 3.0f && p > 3.0e-13f) {
                    const float growth = p / std::max(previous, noise * 0.1f);
                    const float burst = clamp((growth - 2.0f) / 6.0f, 0.0f, 1.0f);
                    target += (1.0f - target) * burst * protect;
                }
                raw_gain[k] = target;
            }
            // A 5-bin triangular smoother reduces isolated tonal artifacts
            // ("musical noise") without large band-wide EQ changes.
            for (int k = 0; k < BINS; ++k) {
                const auto g = [this](int index) -> float {
                    return raw_gain[std::max(0, std::min(index, BINS-1))];
                };
                frequency_gain[k] = (g(k-2) + 2.0f*g(k-1) + 4.0f*g(k)
                    + 2.0f*g(k+1) + g(k+2)) * 0.1f;
            }
            for (int k = 0; k < BINS; ++k) {
                const float target = clamp(frequency_gain[k], floor_gain, 1.0f);
                // Open quickly for clank/transients, close smoothly in pauses.
                const float smoothing = target > smoothed_gain[k] ? 0.18f : 0.88f;
                smoothed_gain[k] = smoothing*smoothed_gain[k] + (1.0f-smoothing)*target;
                const float applied = std::max(floor_gain, smoothed_gain[k]);
                // Clean prior uses Wiener-estimated useful power; it is NOT fed
                // the aggressive floor attenuation, avoiding feedback pumping.
                prev_signal_power[k] = frame_power[k] * applied * applied;
                for (int ch=0; ch<channels; ++ch) {
                    spectra[ch][k] *= applied;
                    if (k > 0 && k < FFT_SIZE/2) spectra[ch][FFT_SIZE-k] *= applied;
                }
            }
        } else {
            smoothed_gain.fill(1.0f);
            prior_snr.fill(0.0f);
            prev_signal_power.fill(0.0f);
        }
        for (int k=0; k<BINS; ++k) prev_power[k] = frame_power[k];

        // Weighted overlap-add into preallocated ring; frame begins at the
        // CURRENT output sample, resulting in 511 samples of fixed latency.
        const int output_pos = static_cast<int>(processed_frames-1) & (FFT_SIZE-1);
        for (int ch=0; ch<channels; ++ch) {
            fft(spectra[ch], true);
            for (int i=0; i<FFT_SIZE; ++i) {
                const int position = (output_pos+i) & (FFT_SIZE-1);
                output_ring[ch][position] += spectra[ch][i].real() * window[i] / SYNTHESIS_NORM;
            }
        }
    }

    void process(const float* input, float* output, int frames, int channels) {
        if (!input || !output || frames <= 0 || (channels !=1 && channels !=2)) return;
        const float strength = clamp(wyldfx_parameter(1),0,100)*0.01f;
        const float max_reduction = clamp(wyldfx_parameter(2),0,30);
        const float protect = clamp(wyldfx_parameter(3),0,100)*0.01f;
        const float mix = clamp(wyldfx_parameter(4),0,100)*0.01f;
        const bool learn_high = wyldfx_parameter(0) > 0.5f;
        if (learn_high && !learn_was_high) start_learning();
        if (!learn_high && learning) { learning = false; learn_frames = 0; }
        learn_was_high = learn_high;
        // Intentionally avoid live channel-layout changes: the host prepares the layout.
        for (int frame=0; frame<frames; ++frame) {
            const int pos = static_cast<int>(processed_frames) & (FFT_SIZE-1);
            for (int ch=0; ch<channels; ++ch)
                input_ring[ch][pos] = finite_sample(input[frame*channels+ch]);
            ++processed_frames;
            if (processed_frames >= FFT_SIZE && (processed_frames - FFT_SIZE)%HOP_SIZE == 0)
                process_fft_frame(channels, strength * mix, max_reduction, protect);
            for (int ch=0; ch<channels; ++ch) {
                float result = output_ring[ch][pos];
                output_ring[ch][pos] = 0;
                // Frequency-domain dry/wet blend is performed in process_fft_frame.
                output[frame*channels+ch] = finite_sample(result);
            }
        }
    }
};

Denoiser denoiser;
ProfileState state_buffer{};
} // namespace

extern "C" const char* wyldfx_name() { return "Wyldtone Noise Reduction"; }
extern "C" const ParameterSpec* wyldfx_parameters() { return PARAMETERS; }
extern "C" int32_t wyldfx_parameter_count() { return PARAM_COUNT; }
extern "C" void wyldfx_prepare(int32_t rate, int32_t channels, int32_t) { denoiser.prepare(rate, channels); }
extern "C" void wyldfx_process(const float* in, float* out, int32_t frames, int32_t chans) {
    denoiser.process(in,out,frames,chans);
}

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define OPTIONAL_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define OPTIONAL_EXPORT
#endif

extern "C" OPTIONAL_EXPORT uint32_t plugin_state_version() { return 1; }
extern "C" OPTIONAL_EXPORT int32_t plugin_state_capture() {
    state_buffer.magic = STATE_MAGIC;
    state_buffer.bins = BINS;
    state_buffer.valid = denoiser.profile_ready ? 1 : 0;
    state_buffer.sample_rate = denoiser.profile_rate;
    for (int k=0;k<BINS;++k) state_buffer.noise_power[k] = denoiser.noise_power[k];
    return sizeof(state_buffer);
}
extern "C" OPTIONAL_EXPORT int32_t plugin_state_buffer_ptr() {
    return static_cast<int32_t>(reinterpret_cast<uintptr_t>(&state_buffer));
}
extern "C" OPTIONAL_EXPORT int32_t plugin_state_prepare_load(int32_t size) {
    return size == static_cast<int32_t>(sizeof(state_buffer)) ?
        static_cast<int32_t>(reinterpret_cast<uintptr_t>(&state_buffer)) : 0;
}
extern "C" OPTIONAL_EXPORT void plugin_state_commit_load(int32_t size) {
    if (size != static_cast<int32_t>(sizeof(state_buffer)) ||
        state_buffer.magic != STATE_MAGIC || state_buffer.bins != BINS || state_buffer.valid > 1) return;
    for (int k=0;k<BINS;++k)
        if (!std::isfinite(state_buffer.noise_power[k]) || state_buffer.noise_power[k] < 0 ||
            state_buffer.noise_power[k] > 1.0e30f) return;
    denoiser.profile_ready = state_buffer.valid != 0 &&
        state_buffer.sample_rate == static_cast<uint32_t>(denoiser.sample_rate);
    denoiser.profile_rate = state_buffer.sample_rate;
    // State restore should not start a new capture from a saved Learn=On toggle.
    if (denoiser.profile_ready) denoiser.learn_was_high = true;
    for (int k=0;k<BINS;++k) denoiser.noise_power[k] = state_buffer.noise_power[k];
    denoiser.smoothed_gain.fill(1.0f);
    denoiser.prev_signal_power.fill(0.0f);
    denoiser.prior_snr.fill(0.0f);
}

#ifdef WYLDFX_NATIVE_TEST
// Test-only hooks. None are exported by the production Emscripten build.
extern "C" const void* wyldfx_test_profile_state_bytes() { return &state_buffer; }
extern "C" void wyldfx_test_forget_profile() {
    denoiser.profile_ready = false;
    denoiser.noise_power.fill(0);
    denoiser.smoothed_gain.fill(1);
    denoiser.prev_signal_power.fill(0);
    denoiser.prior_snr.fill(0);
}
#endif
