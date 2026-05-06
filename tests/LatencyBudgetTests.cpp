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

void convertsSamplesToMilliseconds()
{
    assert(nearlyEqual(LatencyBudgetCalculator::samplesToMilliseconds(480, 48000.0), 10.0));
    assert(nearlyEqual(LatencyBudgetCalculator::samplesToMilliseconds(240, 48000.0), 5.0));
}

void buildsBalancedLatencyBreakdown()
{
    const auto settings = LatencyBudgetCalculator::presetSettings(LatencyPreset::Balanced);
    const auto breakdown = LatencyBudgetCalculator::calculate(makeTestDevice(), settings);

    assert(nearlyEqual(breakdown.inputDeviceMs, 10.0));
    assert(nearlyEqual(breakdown.inputBufferMs, 5.0));
    assert(nearlyEqual(breakdown.outputBufferMs, 5.0));
    assert(nearlyEqual(breakdown.outputDeviceMs, 10.0));
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

void customSettingsArePreserved()
{
    AnalysisLatencySettings settings;
    settings.preset = LatencyPreset::Custom;
    settings.analysisWindowMs = 12.0;
    settings.phonemeLookaheadMs = 34.0;
    settings.phonemeStabilizationMs = 5.0;
    settings.pitchSmoothingMs = 6.0;
    settings.renderQueueMs = 7.0;

    const auto breakdown = LatencyBudgetCalculator::calculate({}, settings);

    assert(nearlyEqual(breakdown.analysisDecisionDelayMs(), 51.0));
    assert(nearlyEqual(breakdown.endToEndMonitoringLatencyMs(), 64.0));
}

void namesPresetsForUi()
{
    assert(LatencyBudgetCalculator::presetName(LatencyPreset::LowLatency) == "Low latency");
    assert(LatencyBudgetCalculator::presetName(LatencyPreset::ExperimentalLongLookahead) ==
           "Experimental long lookahead");
}

void rejectsInvalidSampleRates()
{
    bool threw = false;
    try {
        LatencyBudgetCalculator::samplesToMilliseconds(128, 0.0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    assert(threw);
}

} // namespace

int main()
{
    convertsSamplesToMilliseconds();
    buildsBalancedLatencyBreakdown();
    experimentalPresetCanReachLongLookahead();
    customSettingsArePreserved();
    namesPresetsForUi();
    rejectsInvalidSampleRates();

    std::cout << "LatencyBudget tests passed\n";
    return 0;
}
