#include <Voice2VocalSynth/LatencyBudget.h>

#include <cassert>
#include <cmath>
#include <iostream>

namespace
{
using namespace Voice2VocalSynth;

bool nearlyEqual(double actual, double expected, double tolerance = 0.001)
{
    return std::abs(actual - expected) <= tolerance;
}

AudioDeviceLatency makeTestDevice()
{
    AudioDeviceLatency device;
    device.sampleRateHz = 48000.0;
    device.inputBufferSizeSamples = 240;
    device.outputBufferSizeSamples = 240;
    device.inputDeviceLatencySamples = 480;
    device.outputDeviceLatencySamples = 480;
    return device;
}

void buildsBalancedLatencyBreakdown()
{
    const auto settings = LatencyBudgetCalculator::presetSettings(LatencyPreset::Balanced);
    const auto breakdown = LatencyBudgetCalculator::calculate(makeTestDevice(), settings);

    assert(nearlyEqual(breakdown.inputDeviceMs, 10.0));
    assert(nearlyEqual(breakdown.inputBufferMs, 5.0));
    assert(nearlyEqual(breakdown.analysisDecisionDelayMs(), 135.0));
    assert(nearlyEqual(breakdown.endToEndMonitoringLatencyMs(), 170.0));
    assert(breakdown.components().size() == 9);
}

void experimentalPresetCanReachLongLookahead()
{
    const auto settings = LatencyBudgetCalculator::presetSettings(LatencyPreset::ExperimentalLongLookahead);
    const auto breakdown = LatencyBudgetCalculator::calculate(makeTestDevice(), settings);

    assert(nearlyEqual(breakdown.analysisDecisionDelayMs(), 915.0));
    assert(nearlyEqual(breakdown.endToEndMonitoringLatencyMs(), 1000.0));
}

void rejectsInvalidSampleRates()
{
    bool threw = false;
    try {
        [[maybe_unused]] const auto ignored =
            LatencyBudgetCalculator::samplesToMilliseconds(128, 0.0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    assert(threw);
}

} // namespace

int main()
{
    buildsBalancedLatencyBreakdown();
    experimentalPresetCanReachLongLookahead();
    rejectsInvalidSampleRates();

    std::cout << "LatencyBudget tests passed\n";
    return 0;
}
