#include "Voice2VocalSynth/LatencyBudget.h"

#include <stdexcept>
#include <utility>

namespace Voice2VocalSynth
{
namespace
{

double requireNonNegative(double value, const char* fieldName)
{
    if (value < 0.0) {
        throw std::invalid_argument(std::string(fieldName) + " must not be negative");
    }

    return value;
}

LatencyComponent makeComponent(std::string name, double milliseconds)
{
    return {std::move(name), requireNonNegative(milliseconds, "latency component")};
}

} // namespace

double LatencyBreakdown::analysisDecisionDelayMs() const noexcept
{
    return inputDeviceMs + inputBufferMs + analysisWindowMs + phonemeLookaheadMs +
           phonemeStabilizationMs;
}

double LatencyBreakdown::outputPathDelayMs() const noexcept
{
    return outputBufferMs + outputDeviceMs;
}

double LatencyBreakdown::endToEndMonitoringLatencyMs() const noexcept
{
    return analysisDecisionDelayMs() + pitchSmoothingMs + renderQueueMs + outputPathDelayMs();
}

std::vector<LatencyComponent> LatencyBreakdown::components() const
{
    return {
        makeComponent("Input device latency", inputDeviceMs),
        makeComponent("Input buffer latency", inputBufferMs),
        makeComponent("Analysis window", analysisWindowMs),
        makeComponent("Phoneme lookahead", phonemeLookaheadMs),
        makeComponent("Phoneme stabilization", phonemeStabilizationMs),
        makeComponent("Pitch smoothing", pitchSmoothingMs),
        makeComponent("Render queue", renderQueueMs),
        makeComponent("Output buffer latency", outputBufferMs),
        makeComponent("Output device latency", outputDeviceMs),
    };
}

AnalysisLatencySettings LatencyBudgetCalculator::presetSettings(LatencyPreset preset)
{
    AnalysisLatencySettings settings;
    settings.preset = preset;

    switch (preset) {
        case LatencyPreset::LowLatency:
            settings.analysisWindowMs = 30.0;
            settings.phonemeLookaheadMs = 20.0;
            settings.phonemeStabilizationMs = 10.0;
            settings.pitchSmoothingMs = 5.0;
            settings.renderQueueMs = 5.0;
            break;
        case LatencyPreset::Balanced:
            settings.analysisWindowMs = 60.0;
            settings.phonemeLookaheadMs = 40.0;
            settings.phonemeStabilizationMs = 20.0;
            settings.pitchSmoothingMs = 10.0;
            settings.renderQueueMs = 10.0;
            break;
        case LatencyPreset::HighAccuracy:
            settings.analysisWindowMs = 100.0;
            settings.phonemeLookaheadMs = 100.0;
            settings.phonemeStabilizationMs = 40.0;
            settings.pitchSmoothingMs = 20.0;
            settings.renderQueueMs = 15.0;
            break;
        case LatencyPreset::ExperimentalLongLookahead:
            settings.analysisWindowMs = 200.0;
            settings.phonemeLookaheadMs = 600.0;
            settings.phonemeStabilizationMs = 100.0;
            settings.pitchSmoothingMs = 50.0;
            settings.renderQueueMs = 20.0;
            break;
        case LatencyPreset::Custom:
            break;
    }

    return settings;
}

LatencyBreakdown LatencyBudgetCalculator::calculate(const AudioDeviceLatency& audioDevice,
                                                    const AnalysisLatencySettings& analysis)
{
    if (audioDevice.sampleRateHz <= 0.0) {
        throw std::invalid_argument("sampleRateHz must be positive");
    }

    LatencyBreakdown breakdown;
    breakdown.inputDeviceMs = samplesToMilliseconds(audioDevice.inputDeviceLatencySamples,
                                                    audioDevice.sampleRateHz);
    breakdown.inputBufferMs = samplesToMilliseconds(audioDevice.inputBufferSizeSamples,
                                                    audioDevice.sampleRateHz);
    breakdown.analysisWindowMs = requireNonNegative(analysis.analysisWindowMs, "analysisWindowMs");
    breakdown.phonemeLookaheadMs = requireNonNegative(analysis.phonemeLookaheadMs,
                                                      "phonemeLookaheadMs");
    breakdown.phonemeStabilizationMs = requireNonNegative(analysis.phonemeStabilizationMs,
                                                          "phonemeStabilizationMs");
    breakdown.pitchSmoothingMs = requireNonNegative(analysis.pitchSmoothingMs, "pitchSmoothingMs");
    breakdown.renderQueueMs = requireNonNegative(analysis.renderQueueMs, "renderQueueMs");
    breakdown.outputBufferMs = samplesToMilliseconds(audioDevice.outputBufferSizeSamples,
                                                     audioDevice.sampleRateHz);
    breakdown.outputDeviceMs = samplesToMilliseconds(audioDevice.outputDeviceLatencySamples,
                                                     audioDevice.sampleRateHz);

    return breakdown;
}

double LatencyBudgetCalculator::samplesToMilliseconds(int samples, double sampleRateHz)
{
    if (samples < 0) {
        throw std::invalid_argument("samples must not be negative");
    }
    if (sampleRateHz <= 0.0) {
        throw std::invalid_argument("sampleRateHz must be positive");
    }

    return (static_cast<double>(samples) * 1000.0) / sampleRateHz;
}

std::string LatencyBudgetCalculator::presetName(LatencyPreset preset)
{
    switch (preset) {
        case LatencyPreset::LowLatency:
            return "Low latency";
        case LatencyPreset::Balanced:
            return "Balanced";
        case LatencyPreset::HighAccuracy:
            return "High accuracy";
        case LatencyPreset::ExperimentalLongLookahead:
            return "Experimental long lookahead";
        case LatencyPreset::Custom:
            return "Custom";
    }

    return "Custom";
}

} // namespace Voice2VocalSynth
