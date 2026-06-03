#include <Voice2VocalSynth/SimplePitchEstimator.h>
#include <Voice2VocalSynth/WhistleDetector.h>

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
using namespace Voice2VocalSynth;

std::vector<float> makeSine(double sampleRate, double hz, int numSamples, double amplitude = 0.5)
{
    std::vector<float> out(static_cast<std::size_t>(numSamples));
    for (int i = 0; i < numSamples; ++i) {
        const double phase = 2.0 * 3.14159265358979323846 * hz * static_cast<double>(i) / sampleRate;
        out[static_cast<std::size_t>(i)] = static_cast<float>(amplitude * std::sin(phase));
    }
    return out;
}

std::vector<float> makeHarmonicStack(double sampleRate, double f0, int numSamples)
{
    std::vector<float> out(static_cast<std::size_t>(numSamples), 0.0F);
    const double amps[] = {1.0, 0.55, 0.35, 0.22, 0.15};
    const int harmonics[] = {1, 2, 3, 4, 5};
    for (int h = 0; h < 5; ++h) {
        const auto partial = makeSine(sampleRate, f0 * static_cast<double>(harmonics[h]), numSamples,
                                    0.18 * amps[h]);
        for (int i = 0; i < numSamples; ++i) {
            out[static_cast<std::size_t>(i)] += partial[static_cast<std::size_t>(i)];
        }
    }
    return out;
}

void pure_tone_classified_as_whistle()
{
    constexpr double sr = 48000.0;
    constexpr int n = 4096;
    const auto buf = makeSine(sr, 1000.0, n);
    const auto pitch = estimatePitchFromMono(buf.data(), n, sr);
    assert(pitch.confidence > 0.4);

    WhistleDetector detector;
    const auto obs = detector.analyze(buf.data(), n, sr, 0.1, pitch);
    assert(obs.is_whistle);
    assert(obs.confidence > 0.4F);
}

void harmonic_voiced_stack_not_whistle()
{
    constexpr double sr = 48000.0;
    constexpr int n = 4096;
    const auto buf = makeHarmonicStack(sr, 600.0, n);
    const auto pitch = estimatePitchFromMono(buf.data(), n, sr);

    const WhistleDetector detector;
    const auto obs = detector.analyze(buf.data(), n, sr, 0.1, pitch);
    assert(!obs.is_whistle);
}

void boundary_state_machine_emits_on_off()
{
    constexpr double sr = 48000.0;
    constexpr int n = 4096;
    const auto buf = makeSine(sr, 1000.0, n);

    WhistleDetector detector;
    WhistleBoundaryEvent ev;
    for (int i = 0; i < 12; ++i) {
        const auto pitch = estimatePitchFromMono(buf.data(), n, sr);
        detector.observe(detector.analyze(buf.data(), n, sr, 0.01 * static_cast<double>(i), pitch));
    }
    assert(detector.try_pop_boundary(ev));
    assert(ev.active);

    for (int k = 0; k < 12; ++k) {
        WhistleObservation silent;
        silent.stream_time_seconds = 0.08 + 0.01 * static_cast<double>(k);
        detector.observe(silent);
    }
    assert(detector.try_pop_boundary(ev));
    assert(!ev.active);
}

} // namespace

int main()
{
    pure_tone_classified_as_whistle();
    harmonic_voiced_stack_not_whistle();
    boundary_state_machine_emits_on_off();
    std::cout << "WhistleDetector tests passed\n";
    return 0;
}
