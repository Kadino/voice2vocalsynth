#pragma once

#include "Voice2VocalSynth/LibriSpeechPlayback.h"
#include "Voice2VocalSynth/PhonemeEvaluation.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Voice2VocalSynth
{

struct LivePhonemeFrameRecord
{
    PhonemeFrame frame;
    std::string backend;
    std::int64_t steadyNs = 0;
};

struct LiveBackendLatencyRecord
{
    std::string backend;
    double streamTimeSeconds = 0.0;
    std::int64_t steadyNs = 0;
    double lagMs = 0.0;
    bool ok = false;
};

struct LiveDeviceSettingsRecord
{
    double sampleRateHz = 0.0;
    int bufferSamples = 0;
    int inputLatencySamples = 0;
    int outputLatencySamples = 0;
    int inputBufferSamples = 0;
    int outputBufferSamples = 0;
};

struct LivePhonemeLog
{
    std::string sessionBackend;
    std::int64_t sessionSteadyNs = 0;
    double sessionStreamTimeSeconds = 0.0;
    std::vector<LivePhonemeFrameRecord> phonemeFrames;
    std::vector<LiveBackendLatencyRecord> backendLatencies;
    std::optional<LiveDeviceSettingsRecord> deviceSettings;
    bool hasLatencyMeasurement = false;
    bool latencyMeasurementValid = false;
    double measuredRoundTripMs = 0.0;
    double estimatedEndToEndMs = 0.0;
};

struct LivePhonemeLogLoadResult
{
    bool ok = false;
    LivePhonemeLog log;
    std::string error;
};

[[nodiscard]] LivePhonemeLogLoadResult parseLivePhonemeLogJsonl(std::string_view jsonl);
[[nodiscard]] LivePhonemeLogLoadResult loadLivePhonemeLogJsonl(
    const std::filesystem::path& path);

[[nodiscard]] std::vector<PhonemeFrame> convertLivePhonemeFrames(
    const LivePhonemeLog& log,
    std::string_view backend);

struct LiveLatencyDistribution
{
    std::size_t sampleCount = 0;
    double p50Ms = 0.0;
    double p95Ms = 0.0;
    double p99Ms = 0.0;
    double maxMs = 0.0;
};

struct LiveVerificationLatencyMetrics
{
    double endToEndMs = 0.0;
    std::string endToEndSource;
    LiveLatencyDistribution decision;
    LiveLatencyDistribution backend;
};

struct LiveVerificationGateOptions
{
    double maxEndToEndLatencyMs = 1000.0;
    std::optional<double> minF1;
    std::optional<double> maxMeanOnsetErrorMs;
    std::optional<double> maxP95OnsetErrorMs;
    std::optional<double> maxMeanEndErrorMs;
    std::optional<double> maxP95EndErrorMs;
    std::optional<double> maxMeanDurationErrorMs;
    std::optional<double> maxMissedConsonantRate;
    bool rejectPlaceholderBackend = true;
};

struct LiveVerificationGateResult
{
    bool passed = false;
    std::vector<std::string> failures;
};

struct LiveUtteranceMetrics
{
    std::string utteranceId;
    PhonemeEvaluationMetrics quality;
};

struct LivePhonemeVerificationReport
{
    std::string backend;
    std::size_t predictionCount = 0;
    PhonemeEvaluationMetrics quality;
    LiveVerificationLatencyMetrics latency;
    LiveVerificationGateResult gates;
    std::vector<LiveUtteranceMetrics> utterances;
    std::vector<PhonemeFrame> predictions;
};

struct LivePhonemeVerificationResult
{
    bool ok = false;
    LivePhonemeVerificationReport report;
    std::string error;
};

[[nodiscard]] LivePhonemeVerificationResult verifyLivePhonemeRun(
    const LivePhonemeLog& log,
    const LibriSpeechPlaybackPlan& playback,
    const std::filesystem::path& labelsRoot,
    std::string_view backend,
    const LiveVerificationGateOptions& gates);

[[nodiscard]] std::string livePhonemeVerificationMetricsJson(
    const LivePhonemeVerificationReport& report);
[[nodiscard]] std::string livePhonemeVerificationMarkdown(
    const LivePhonemeVerificationReport& report);

[[nodiscard]] bool writeLivePhonemeVerificationOutputs(
    const LivePhonemeVerificationReport& report,
    const std::filesystem::path& predictionsPath,
    const std::filesystem::path& metricsPath,
    const std::filesystem::path& reportPath,
    std::string& error);

} // namespace Voice2VocalSynth
