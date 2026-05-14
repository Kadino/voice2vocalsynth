#include "Voice2VocalSynth/SimplePitchEstimator.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Voice2VocalSynth
{
namespace
{

[[nodiscard]] double rmsMono(const float* samples, int numSamples)
{
    if (numSamples <= 0 || samples == nullptr) {
        return 0.0;
    }
    double acc = 0.0;
    for (int i = 0; i < numSamples; ++i) {
        const double s = static_cast<double>(samples[i]);
        acc += s * s;
    }
    return std::sqrt(acc / static_cast<double>(numSamples));
}

} // namespace

SimplePitchEstimate estimatePitchFromMono(const float* samples,
                                           int numSamples,
                                           const double sampleRateHz)
{
    SimplePitchEstimate out;
    if (samples == nullptr || numSamples < simplePitchEstimatorMinSamples() || sampleRateHz <= 0.0) {
        return out;
    }

    const double rms = rmsMono(samples, numSamples);
    if (rms < 1.0e-4) {
        return out;
    }

    const int minHz = 65;
    const int maxHz = 1100;
    const int minLag = static_cast<int>(std::floor(sampleRateHz / static_cast<double>(maxHz)));
    const int maxLag = static_cast<int>(std::ceil(sampleRateHz / static_cast<double>(minHz)));
    if (minLag < 1 || maxLag <= minLag || maxLag >= numSamples) {
        return out;
    }

    std::vector<float> window(static_cast<std::size_t>(numSamples));
    for (int i = 0; i < numSamples; ++i) {
        const double phase =
            6.28318530717958647692 * static_cast<double>(i) / static_cast<double>(numSamples - 1 > 0 ? numSamples - 1 : 1);
        const double hann = 0.5 * (1.0 - std::cos(phase));
        window[static_cast<std::size_t>(i)] = static_cast<float>(samples[i] * static_cast<float>(hann));
    }

    double bestNorm = 0.0;
    int bestLag = minLag;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        double acc = 0.0;
        double n0 = 0.0;
        double n1 = 0.0;
        const int count = numSamples - lag;
        for (int i = 0; i < count; ++i) {
            const double a = static_cast<double>(window[static_cast<std::size_t>(i)]);
            const double b = static_cast<double>(window[static_cast<std::size_t>(i + lag)]);
            acc += a * b;
            n0 += a * a;
            n1 += b * b;
        }
        const double denom = std::sqrt(std::max(1.0e-24, n0 * n1));
        const double norm = acc / denom;
        if (norm > bestNorm) {
            bestNorm = norm;
            bestLag = lag;
        }
    }

    if (bestLag <= 0 || bestNorm <= 0.2) {
        return out;
    }

    out.frequencyHz = sampleRateHz / static_cast<double>(bestLag);
    out.confidence = std::clamp((bestNorm - 0.2) / 0.8, 0.0, 1.0);
    return out;
}

} // namespace Voice2VocalSynth
