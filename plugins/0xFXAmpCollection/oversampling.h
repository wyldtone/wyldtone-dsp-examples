// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Wyldtone contributors
// Fixed-size, streaming 2x FIR half-band resampler (two cascaded stages for 4x).
#pragma once

namespace amp_os {

// 63-tap Kaiser-windowed half-band low-pass, cutoff at fs/4,
// beta=8.6, unity DC gain. The 31 noncentral odd taps are zero.
// Coefficients are symmetric; expanded even-index taps are listed below.
static constexpr float kEven[32] = {
    -1.3682310342e-05f, 7.367982478e-05f, -0.00021592065572f,
     0.00049873709007f, -0.0010020225852f, 0.0018300546271f,
    -0.0031149912511f, 0.0050232894202f, -0.0077699291801f,
     0.011651144673f, -0.01712112929f, 0.024981912269f,
    -0.036911386447f, 0.057271132152f, -0.10215353194f,
     0.31697309061f, 0.31697309061f, -0.10215353194f,
     0.057271132152f, -0.036911386447f, 0.024981912269f,
    -0.01712112929f, 0.011651144673f, -0.0077699291801f,
     0.0050232894202f, -0.0031149912511f, 0.0018300546271f,
    -0.0010020225852f, 0.00049873709007f, -0.00021592065572f,
     7.367982478e-05f, -1.3682310342e-05f
};
static constexpr float kCenter = 0.49999910598769115f;
struct HalfBand2x {
    float upHistory[32] = {};
    float downHistory[64] = {};
    unsigned upIndex = 0;
    unsigned downIndex = 0;

    inline void up(float in, float& even, float& odd) {
        upIndex = (upIndex + 1u) & 31u;
        upHistory[upIndex] = in;
        float sum = 0.0f;
        for (unsigned i = 0; i < 32; ++i)
            sum += kEven[i] * upHistory[(upIndex - i) & 31u];
        even = 2.0f * sum;
        odd = 2.0f * kCenter * upHistory[(upIndex - 15u) & 31u];
    }

    // Center tap and 32 even taps convolved before decimation by two.
    inline float down(float even, float odd) {
        downIndex = (downIndex + 1u) & 63u;
        downHistory[downIndex] = even;
        float sum = kCenter * downHistory[(downIndex - 31u) & 63u];
        for (unsigned i = 0; i < 32; ++i)
            sum += kEven[i] * downHistory[(downIndex - 2u*i) & 63u];
        downIndex = (downIndex + 1u) & 63u;
        downHistory[downIndex] = odd;
        return sum;
    }
};
// 61-tap half-band stage for the second 2x step. Unlike the first 63-tap
// filter its center (tap 30) lies on the even phase, giving EXACTLY 15 base
// frames of extra roundtrip delay, for 46 whole frames in 4x mode.
static constexpr float kOdd61[30] = {
    0.0000385073974237f, -0.000143086926873f, 0.000371182694112f, -0.000802860607252f,
    0.00154552055460f, -0.00273763716872f, 0.00455485872472f, -0.00722331422076f,
    0.0110509162793f, -0.0165023292920f, 0.0243870866399f, -0.0363861545556f,
    0.0568588697952f, -0.101889901868f, 0.316880515834f, 0.316880515834f,
    -0.101889901868f, 0.0568588697952f, -0.0363861545556f, 0.0243870866399f,
    -0.0165023292920f, 0.0110509162793f, -0.00722331422076f, 0.00455485872472f,
    -0.00273763716872f, 0.00154552055460f, -0.000802860607252f, 0.000371182694112f,
    -0.000143086926873f, 0.0000385073974237f
};
static constexpr float kCenter61 = 0.4999956534400081f;

struct SecondStage2x {
    float upHistory[32] = {};
    float downHistory[64] = {};
    unsigned upIndex = 0, downIndex = 0;
    inline void up(float in, float& even, float& odd) {
        upIndex = (upIndex + 1u) & 31u;
        upHistory[upIndex] = in;
        even = 2.0f * kCenter61 * upHistory[(upIndex - 15u) & 31u];
        float sum = 0.0f;
        for (unsigned i = 0; i < 30; ++i)
            sum += kOdd61[i] * upHistory[(upIndex - i) & 31u];
        odd = 2.0f * sum;
    }
    inline float down(float even, float odd) {
        downIndex = (downIndex + 1u) & 63u;
        downHistory[downIndex] = even;
        float sum = kCenter61 * downHistory[(downIndex - 30u) & 63u];
        for (unsigned i = 0; i < 30; ++i)
            sum += kOdd61[i] * downHistory[(downIndex - (2u*i + 1u)) & 63u];
        downIndex = (downIndex + 1u) & 63u;
        downHistory[downIndex] = odd;
        return sum;
    }
};

struct State {
    HalfBand2x first;
    SecondStage2x second;
};

// FIR roundtrip latency at the host rate: first-stage = 31 frames;
// second-stage = 30 high-rate frames / 2 = 15 host frames.
inline int latencySamples(int factor) {
    return factor == 4 ? 46 : factor == 2 ? 31 : 0;
}

} // namespace amp_os
