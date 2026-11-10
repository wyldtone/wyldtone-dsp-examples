#include "../wyldtone_fx_common/Modulation.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

using namespace wyldfx;

namespace {
constexpr float TWO_PI = 2.0f * PI;
float sr = 48000.0f;
OnePoleHP trackHp;
OnePoleLP trackLp;
OnePoleLP sub1LpA, sub1LpB, sub2LpA, sub2LpB, upLpA, upLpB;
DcBlock sub1Dc, sub2Dc, upDc;
Envelope inputEnvelope;

// Monophonic pitch detector. A held note is not retriggered by an isolated
// spurious zero crossing, especially on the low-level decay of a guitar note.
// Confirmed note changes are applied only after a short fade to silence.
struct MonoPitchTracker {
    int64_t sampleClock = 0;
    int64_t lastCross = -1;
    int64_t belowGateSamples = 0;
    bool armed = false;
    bool hasPitch = false;
    bool retuning = false;
    float period = 0.0f;
    float targetPeriod = 0.0f;
    float candidatePeriod = 0.0f;
    float pendingPeriod = 0.0f;
    float transitionGain = 1.0f;
    int candidateCount = 0;
    int stableCycles = 0;
    float subPhase1 = 0.0f;
    float subPhase2 = 0.0f;
    float upperPhase = 0.0f;

    void reset() { *this = {}; }

    void clockSample(float filtered, float envelope, float threshold, float sampleRate) {
        const float minPeriod = sampleRate / 1050.0f;
        const float maxPeriod = sampleRate / 60.0f;
        const float hysteresis = std::max(0.00045f, envelope * 0.095f);
        const bool strongSignal = envelope > threshold * 2.25f;
        const bool canChangeNote = envelope > std::max(threshold * 3.5f, 0.009f);

        // Keep the last pitch during release. Only after the note has actually
        // faded away can the detector become unlatched for the next note.
        if (envelope < threshold * 1.12f) {
            ++belowGateSamples;
            if (belowGateSamples > static_cast<int64_t>(sampleRate * 0.135f)) {
                hasPitch = false;
                stableCycles = 0;
                candidateCount = 0;
                retuning = false;
                targetPeriod = 0.0f;
                period = 0.0f;
                transitionGain = 1.0f;
                lastCross = -1;
            }
        } else {
            belowGateSamples = 0;
        }

        if (filtered < -hysteresis) armed = true;
        if (armed && filtered > hysteresis) {
            armed = false;
            if (strongSignal && lastCross >= 0 && !retuning) {
                const float measured = static_cast<float>(sampleClock - lastCross);
                if (measured >= minPeriod && measured <= maxPeriod) {
                    if (!hasPitch && targetPeriod <= 0.0f) {
                        targetPeriod = measured;
                        period = measured;
                        stableCycles = 1;
                    } else if (targetPeriod > 0.0f &&
                               measured > targetPeriod * 0.78f &&
                               measured < targetPeriod * 1.28f) {
                        // Small pitch changes (vibrato, string bends) glide
                        // smoothly; an occasional missing crossing cannot
                        // disable a previously valid note.
                        targetPeriod += 0.20f * (measured - targetPeriod);
                        stableCycles = std::min(stableCycles + 1, 8);
                        if (stableCycles >= 3) hasPitch = true;
                        candidateCount = 0;
                    } else if (canChangeNote) {
                        // Switching to a new note requires repeated evidence.
                        // Never accept an octave error from a quiet decay.
                        if (candidateCount > 0 &&
                            measured > candidatePeriod * 0.91f &&
                            measured < candidatePeriod * 1.10f) {
                            candidatePeriod += 0.30f * (measured - candidatePeriod);
                            ++candidateCount;
                        } else {
                            candidatePeriod = measured;
                            candidateCount = 1;
                        }
                        if (candidateCount >= 4) {
                            if (hasPitch) {
                                pendingPeriod = candidatePeriod;
                                retuning = true;
                            } else {
                                targetPeriod = candidatePeriod;
                                period = candidatePeriod;
                                stableCycles = 3;
                                hasPitch = true;
                            }
                            candidateCount = 0;
                        }
                    }
                    // A single invalid cycle does NOT clear hasPitch.
                }
            }
            // Don't restart the clock from sporadic near-silent crossings.
            if (strongSignal) lastCross = sampleClock;
        }

        // On a new note, fade the previous oscillator to (near) zero before
        // changing its frequency; that avoids the digital 'blip' / chirp.
        const float downStep = 1.0f / (sampleRate * 0.012f);
        const float upStep = 1.0f / (sampleRate * 0.018f);
        if (retuning) {
            transitionGain = std::max(0.0f, transitionGain - downStep);
            if (transitionGain <= 0.0f) {
                targetPeriod = pendingPeriod;
                period = pendingPeriod;
                retuning = false;
                stableCycles = 3;
                hasPitch = true;
            }
        } else {
            transitionGain = std::min(1.0f, transitionGain + upStep);
        }

        // Continuous phase and continuously interpolated pitch even while
        // the audio envelope is changing. All three octave voices share it.
        if (targetPeriod > 0.0f) {
            const float slew = 1.0f - smooth_alpha(11.0f, sampleRate);
            period += slew * (targetPeriod - period);
            const float p = clamp(period, minPeriod, maxPeriod);
            subPhase1 += PI / p;           // -1 octave
            subPhase2 += (PI * 0.5f) / p;  // -2 octaves
            upperPhase += (4.0f * PI) / p; // +1 octave
            if (subPhase1 >= TWO_PI) subPhase1 -= TWO_PI;
            if (subPhase2 >= TWO_PI) subPhase2 -= TWO_PI;
            if (upperPhase >= TWO_PI) upperPhase -= TWO_PI;
        }
        ++sampleClock;
    }
    bool locked() const { return hasPitch; }
} tracker;

float wetEnvelope = 0.0f;
constexpr ParameterSpec P[] = {
    // Existing indices and IDs MUST remain stable: preset values are keyed by ID
    // and previous versions also rely on the first seven positions.
    {"dry",             "Dry level",       "%",  0,   100, 80,   1},
    {"sub1",            "Sub -1 octave",   "%",  0,   100, 65,   1},
    {"sub2",            "Sub -2 octaves",  "%",  0,   100, 10,   1},
    {"track_threshold", "Track threshold", "dB", -75, -25, -48, .5f},
    {"tone",            "Sub tone",        "Hz", 100, 2600, 650, 5},
    {"drive",           "Sub drive",       "%",  0,   100, 8,    1},
    {"level",           "Output",          "dB", -18, 12,  -4,  .5f},
    // Appended: old rigs default to 0% octave-up, so their mix is preserved.
    {"oct_up",          "Octave +1",       "%",  0,   100,  0,   1},
};

inline float soft_peak_guard(float x) {
    constexpr float knee = 0.94f;
    constexpr float headroom = 1.0f - knee;
    if (x > knee) return knee + headroom * std::tanh((x - knee) / headroom);
    if (x < -knee) return -knee + headroom * std::tanh((x + knee) / headroom);
    return x;
}
} // namespace

extern "C" const char* wyldfx_name() { return "Classic Analog Octaver"; }
extern "C" const ParameterSpec* wyldfx_parameters() { return P; }
extern "C" int32_t wyldfx_parameter_count() { return static_cast<int32_t>(sizeof(P) / sizeof(P[0])); }

extern "C" void wyldfx_prepare(int32_t rate, int32_t, int32_t) {
    sr = static_cast<float>(std::max(rate, 8000));
    trackHp.reset(); trackLp.reset();
    sub1LpA.reset(); sub1LpB.reset(); sub2LpA.reset(); sub2LpB.reset();
    upLpA.reset(); upLpB.reset();
    sub1Dc.prepare(sr, 18.0f); sub2Dc.prepare(sr, 18.0f);
    upDc.prepare(sr, 18.0f);
    inputEnvelope.reset(); tracker.reset(); wetEnvelope = 0.0f;
}

extern "C" void wyldfx_process(const float* in, float* out, int32_t frames, int32_t chans) {
    if (!in || !out || frames <= 0 || chans <= 0) return;
    const float dry = clamp(wyldfx_parameter(0) * 0.01f, 0.0f, 1.0f);
    const float sub1 = clamp(wyldfx_parameter(1) * 0.01f, 0.0f, 1.0f);
    const float sub2 = clamp(wyldfx_parameter(2) * 0.01f, 0.0f, 1.0f);
    const float threshold = db_to_linear(wyldfx_parameter(3));
    const float tone = wyldfx_parameter(4);
    const float drive = clamp(wyldfx_parameter(5) * 0.01f, 0.0f, 1.0f);
    const float master = db_to_linear(wyldfx_parameter(6));
    const float octUp = clamp(wyldfx_parameter(7) * 0.01f, 0.0f, 1.0f);

    trackHp.set(55.0f, sr);
    trackLp.set(750.0f, sr);
    sub1LpA.set(tone, sr); sub1LpB.set(tone, sr);
    sub2LpA.set(tone * 0.7f, sr); sub2LpB.set(tone * 0.7f, sr);
    // The higher voice gets its own brighter filtering. It is still a
    // monophonic oscillator, NOT a polyphonic OC-5 pitch-shift algorithm.
    const float upCutoff = clamp(tone * 4.0f, 2800.0f, 8200.0f);
    upLpA.set(upCutoff, sr); upLpB.set(upCutoff, sr);

    const float attack = smooth_alpha(4.0f, sr);
    const float release = smooth_alpha(110.0f, sr);
    const float wetAttack = smooth_alpha(17.0f, sr);
    const float wetRelease = smooth_alpha(125.0f, sr);
    const float shape = 1.0f + drive * 2.4f;
    const float shapeNorm = 1.0f / std::tanh(shape);
    const float upShape = 1.4f + drive * 1.5f;
    const float upNorm = 1.0f / std::tanh(upShape);

    for (int32_t i = 0; i < frames; ++i) {
        float mono = 0.0f, peak = 0.0f;
        for (int32_t c = 0; c < chans; ++c) {
            const float x = finite_sample(in[i * chans + c]);
            mono += x;
            peak = std::max(peak, std::fabs(x));
        }
        mono /= static_cast<float>(chans);
        const float e = inputEnvelope.process(peak, attack, release);
        const float filtered = trackLp.run(trackHp.run(mono));
        tracker.clockSample(filtered, e, threshold, sr);

        // Continuous, amplitude-dependent envelope. Tracking cannot
        // re-trigger near the noise floor, and lost cycles cannot toggle the
        // sub voices instantly between on/off while a guitar note decays.
        const float gate = clamp((e - threshold * 1.12f) / (threshold * 3.8f), 0.0f, 1.0f);
        const float gateShape = gate * gate * (3.0f - 2.0f * gate);
        const float amplitude = clamp(e / 0.085f, 0.0f, 1.0f);
        const float target = (tracker.locked() ? 1.0f : 0.0f) * gateShape * amplitude;
        const float follow = target > wetEnvelope ? wetAttack : wetRelease;
        wetEnvelope = target + follow * (wetEnvelope - target);

        float wave1 = std::tanh(std::sin(tracker.subPhase1) * shape) * shapeNorm;
        float wave2 = std::tanh(std::sin(tracker.subPhase2) * shape) * shapeNorm;
        float waveUp = std::tanh(std::sin(tracker.upperPhase) * upShape) * upNorm;
        wave1 = sub1Dc.run(sub1LpB.run(sub1LpA.run(wave1)));
        wave2 = sub2Dc.run(sub2LpB.run(sub2LpA.run(wave2)));
        waveUp = upDc.run(upLpB.run(upLpA.run(waveUp)));
        const float wet = wetEnvelope * tracker.transitionGain *
                          (sub1 * 0.43f * wave1 + sub2 * 0.26f * wave2 +
                           octUp * 0.34f * waveUp);

        for (int32_t c = 0; c < chans; ++c) {
            const int32_t j = i * chans + c;
            const float original = finite_sample(in[j]);
            out[j] = finite_sample(soft_peak_guard((original * dry + wet) * master));
        }
    }
}
