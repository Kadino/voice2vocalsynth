#pragma once

#include <string>
#include <vector>

namespace Voice2VocalSynth
{

enum class LatencyPreset
{
    LowLatency,
    Balanced,
    HighAccuracy,
    ExperimentalLongLookahead,
    Custom
};

struct AudioDeviceLatency
{
    double sampleRateHz = 48000.0;
    int inputBufferSizeSamples = 0;
    int outputBufferSizeSamples = 0;
    int inputDeviceLatencySamples = 0;
    int outputDeviceLatencySamples = 0;
};

struct AnalysisLatencySettings
{
    LatencyPreset preset = LatencyPreset::Balanced;
    double analysisWindowMs = 60.0;
    double phonemeLookaheadMs = 40.0;
    double phonemeStabilizationMs = 20.0;
    double pitchSmoothingMs = 10.0;
    double renderQueueMs = 10.0;
};

struct LatencyComponent
{
    std::string name;
    double milliseconds = 0.0;
};

struct LatencyBreakdown
{
    double inputDeviceMs = 0.0;
    double inputBufferMs = 0.0;
    double analysisWindowMs = 0.0;
    double phonemeLookaheadMs = 0.0;
    double phonemeStabilizationMs = 0.0;
    double pitchSmoothingMs = 0.0;
    double renderQueueMs = 0.0;
    double outputBufferMs = 0.0;
    double outputDeviceMs = 0.0;

    [[nodiscard]] double analysisDecisionDelayMs() const noexcept;
    [[nodiscard]] double outputPathDelayMs() const noexcept;
    [[nodiscard]] double endToEndMonitoringLatencyMs() const noexcept;
    [[nodiscard]] std::vector<LatencyComponent> components() const;
};

class LatencyBudgetCalculator
{
public:
    [[nodiscard]] static AnalysisLatencySettings presetSettings(LatencyPreset preset);
    [[nodiscard]] static LatencyBreakdown calculate(const AudioDeviceLatency& audioDevice,
                                                    const AnalysisLatencySettings& analysis);
    [[nodiscard]] static double samplesToMilliseconds(int samples, double sampleRateHz);
    [[nodiscard]] static std::string presetName(LatencyPreset preset);
};

} // namespace Voice2VocalSynth
