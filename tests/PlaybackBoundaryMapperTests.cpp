#include <Voice2VocalSynth/PlaybackBoundaryMapper.h>

#include <cassert>
#include <cmath>
#include <iostream>

namespace
{
using namespace Voice2VocalSynth;

void maps_analysis_to_playback()
{
    const auto settings = LatencyBudgetCalculator::presetSettings(LatencyPreset::Balanced);
    AudioDeviceLatency device;
    device.sampleRateHz = 48000.0;
    const auto breakdown = LatencyBudgetCalculator::calculate(device, settings);

    const double playback = PlaybackBoundaryMapper::analysisToPlaybackSeconds(1.0, breakdown, 12.0);
    const double expected = 1.0 + (breakdown.endToEndMonitoringLatencyMs() + 12.0) / 1000.0;
    assert(std::abs(playback - expected) < 1.0e-6);
}

void measured_end_to_end_overrides_estimate()
{
    const auto settings = LatencyBudgetCalculator::presetSettings(LatencyPreset::Balanced);
    AudioDeviceLatency device;
    device.sampleRateHz = 48000.0;
    const auto breakdown = LatencyBudgetCalculator::calculate(device, settings);

    const double playback =
        PlaybackBoundaryMapper::analysisToPlaybackSeconds(0.5, breakdown, 0.0, 99.0);
    assert(std::abs(playback - (0.5 + 0.099)) < 1.0e-6);
}

} // namespace

int main()
{
    maps_analysis_to_playback();
    measured_end_to_end_overrides_estimate();
    std::cout << "PlaybackBoundaryMapper tests passed\n";
    return 0;
}
