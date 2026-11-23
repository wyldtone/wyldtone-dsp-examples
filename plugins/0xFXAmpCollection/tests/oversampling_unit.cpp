// SPDX-License-Identifier: Apache-2.0
#include "../oversampling.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

// Test streaming FIR identity with a passthrough 'amp': chunking must not
// change the samples, even across different host block sizes.
std::vector<float> roundtrip(const std::vector<float>& input, int factor, int chunk) {
    amp_os::State state;
    std::vector<float> result(input.size());
    for (size_t start = 0; start < input.size(); start += chunk) {
        const size_t n = std::min<size_t>(chunk, input.size() - start);
        for (size_t i = 0; i < n; ++i) {
            float a, b, c, d;
            if (factor == 2) {
                state.first.up(input[start + i], a, b);
                result[start + i] = state.first.down(a, b);
            } else {
                state.first.up(input[start + i], a, b);
                state.second.up(a, c, d);
                const float o0 = state.second.down(c, d);
                state.second.up(b, c, d);
                const float o1 = state.second.down(c, d);
                result[start + i] = state.first.down(o0, o1);
            }
        }
    }
    return result;
}
int main() {
    constexpr int n = 48000;
    constexpr double pi = 3.14159265358979323846;
    std::vector<float> input(n);
    for (int i = 0; i < n; ++i)
        input[i] = static_cast<float>(0.1 + std::sin(2 * pi * 997 * i / 48000.0));
    for (int factor : {2, 4}) {
        const auto whole = roundtrip(input, factor, n);
        const auto chunks = roundtrip(input, factor, 73);
        for (int i = 0; i < n; ++i) {
            assert(std::isfinite(chunks[i]));
            assert(std::abs(chunks[i] - whole[i]) < 1e-6f);
        }
        double signal = 0.0, error = 0.0;
        // Fractional delay at 4x needs separate interpolation; this tolerance
        // only catches broken resampler operation, not audio transparency.
        const int lag = factor == 2 ? 31 : 47;
        for (int i = 1500; i < n; ++i) {
            const double diff = input[i - lag] - whole[i];
            error += diff * diff;
            signal += input[i - lag] * input[i - lag];
        }
        const double relativeError = std::sqrt(error / signal);
        assert(relativeError < (factor == 2 ? 0.001 : 0.08));
        std::printf("%dx streaming FIR test passed (RMS error %.6g)\\n",
                    factor, relativeError);
    }
}