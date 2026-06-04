#include <Voice2VocalSynth/LoopbackLatencyMeasurer.h>
#include <Voice2VocalSynth/MeasuredLatency.h>

#include <cassert>
#include <cmath>
#include <deque>
#include <iostream>

namespace
{
using namespace Voice2VocalSynth;

void detects_known_delay_in_synthetic_loopback()
{
    LoopbackLatencyMeasurerOptions opt;
    opt.measurement_samples = 4096;
    opt.max_lag_samples = 2048;
    opt.probe_amplitude = 0.25F;
    opt.min_correlation = 0.2;

    constexpr double kRate = 48000.0;
    constexpr int kDelay = 512;

    LoopbackLatencyMeasurer measurer(opt);
    measurer.begin();

    std::deque<float> loop(static_cast<std::size_t>(kDelay), 0.0F);
    float out = 0.0F;
    float in = 0.0F;
    int guard = 0;
    while (measurer.is_measuring() && guard++ < 200000) {
        in = loop.front();
        loop.pop_front();
        out = 0.0F;
        measurer.process(&out, &in, 1, kRate);
        loop.push_back(out);
    }

    assert(measurer.has_result());
    const auto result = measurer.result();
    assert(result.valid);
    assert(std::abs(result.lag_samples - kDelay) <= 3);
    assert(std::abs(result.round_trip_ms - (static_cast<double>(kDelay) * 1000.0 / kRate)) < 0.5);
    assert(result.correlation >= opt.min_correlation);
}

void measured_summary_prefers_loopback()
{
    LatencyBreakdown est;
    est.analysisWindowMs = 10.0;
    est.phonemeLookaheadMs = 10.0;
    est.phonemeStabilizationMs = 10.0;
    est.pitchSmoothingMs = 5.0;
    est.renderQueueMs = 5.0;

    MeasuredLatencySummary summary;
    summary.estimated = est;
    summary.loopback.valid = true;
    summary.loopback.round_trip_ms = 42.0;
    summary.inference_jitter_ms = 3.0;

    assert(std::abs(summary.effective_end_to_end_ms() - 42.0) < 1.0e-6);
    assert(std::abs(summary.playback_mapping_ms() - 45.0) < 1.0e-6);
}

} // namespace

int main()
{
    detects_known_delay_in_synthetic_loopback();
    measured_summary_prefers_loopback();
    std::cout << "LoopbackLatencyMeasurer tests passed\n";
    return 0;
}
