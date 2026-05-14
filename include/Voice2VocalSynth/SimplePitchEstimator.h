#pragma once

namespace Voice2VocalSynth
{

struct SimplePitchEstimate
{
    double frequencyHz = 0.0;
    double confidence = 0.0;
};

/// Single-channel, short-window autocorrelation pitch estimate for live prototyping.
/// Expects `numSamples` at least `minSamples()` for a stable result; otherwise returns confidence 0.
[[nodiscard]] SimplePitchEstimate estimatePitchFromMono(const float* samples,
                                                        int numSamples,
                                                        double sampleRateHz);

[[nodiscard]] constexpr int simplePitchEstimatorMinSamples() noexcept
{
    return 2048;
}

} // namespace Voice2VocalSynth
