#include <Voice2VocalSynth/SimplePitchEstimator.h>

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
using namespace Voice2VocalSynth;

void sine440HzNearExpectedPitch()
{
    constexpr double sampleRate = 48000.0;
    constexpr double frequency = 440.0;
    constexpr int n = 8192;
    std::vector<float> buf(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        buf[static_cast<std::size_t>(i)] =
            static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979323846 * frequency * static_cast<double>(i) / sampleRate));
    }
    const auto est = estimatePitchFromMono(buf.data(), n, sampleRate);
    assert(est.confidence > 0.25);
    assert(est.frequencyHz > 420.0 && est.frequencyHz < 460.0);
}

void shortBufferReturnsZeroConfidence()
{
    std::vector<float> buf(128, 0.1F);
    const auto est = estimatePitchFromMono(buf.data(), static_cast<int>(buf.size()), 48000.0);
    assert(est.confidence == 0.0);
}

} // namespace

int main()
{
    sine440HzNearExpectedPitch();
    shortBufferReturnsZeroConfidence();
    std::cout << "SimplePitchEstimator tests passed\n";
    return 0;
}
