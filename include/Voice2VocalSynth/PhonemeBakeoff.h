#pragma once

#include "Voice2VocalSynth/PhonemeBackend.h"
#include "Voice2VocalSynth/PhonemeEvaluation.h"
#include "Voice2VocalSynth/PhonemeTemporalStabilizer.h"

#include <memory>
#include <string>
#include <vector>

namespace Voice2VocalSynth
{

struct PhonemeBackendPipelineResult
{
    bool ok = false;
    std::string error;
    std::string backendName;
    std::vector<PhonemeFrame> rawFrames;
    std::vector<PhonemeFrame> stabilizedFrames;
    std::vector<double> backendLatenciesMs;
    double meanBackendLatencyMs = 0.0;
    double p95BackendLatencyMs = 0.0;
};

struct PhonemeBakeoffEntry
{
    std::string backendName;
    PhonemeEvaluationMetrics rawMetrics;
    PhonemeEvaluationMetrics stabilizedMetrics;
    double meanBackendLatencyMs = 0.0;
    double p95BackendLatencyMs = 0.0;
};

struct PhonemeBakeoffReport
{
    std::vector<PhonemeBakeoffEntry> entries;
};

[[nodiscard]] PhonemeFrame phonemeObservationToFrame(const PhonemeTemporalObservation& observation,
                                                       double segmentSeconds);

[[nodiscard]] PhonemeBackendPipelineResult runPhonemeBackendOnMono(
    IPhonemeBackend& backend,
    const std::vector<float>& mono,
    double sampleRateHz,
    const PhonemeTemporalStabilizerOptions& stabilizerOptions = {});

[[nodiscard]] PhonemeBakeoffReport runPhonemeBakeoff(
    const std::vector<PhonemeFrame>& reference,
    const std::vector<IPhonemeBackend*>& backends,
    const std::vector<float>& mono,
    double sampleRateHz,
    const PhonemeEvaluationOptions& options = {},
    const PhonemeTemporalStabilizerOptions& stabilizerOptions = {});

[[nodiscard]] std::string phonemeBakeoffReportToJson(const PhonemeBakeoffReport& report);

} // namespace Voice2VocalSynth
