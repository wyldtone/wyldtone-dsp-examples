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
struct State {
    HalfBand2x first;
    HalfBand2x second;
};
// FIR latency: 31 host samples at 2x, 46.5 host samples at 4x.
// The current WAP ABI does not report this to the host; beware parallel paths.
inline float latencySamples(int factor) {
    return factor == 4 ? 46.5f : factor == 2 ? 31.0f : 0.0f;
}
} // namespace amp_os
