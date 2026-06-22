#pragma once

#include "Voice2VocalSynth/PhonemeFrame.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Voice2VocalSynth
{

struct PhonemeEvaluationOptions
{
    double maxOnsetErrorSeconds = 0.05;
    double minOverlapSeconds = 0.02;
};

struct PhonemeEvaluationMetrics
{
    std::size_t referenceCount = 0;
    std::size_t predictionCount = 0;
    std::size_t matchedCount = 0;
    std::size_t substitutionOrTimingErrorCount = 0;
    std::size_t missedCount = 0;
    std::size_t falsePositiveCount = 0;
    std::size_t referenceConsonantCount = 0;
    std::size_t missedConsonantCount = 0;
    double precision = 0.0;
    double recall = 0.0;
    double f1 = 0.0;
    double falsePositiveRate = 0.0;
    double missedConsonantRate = 0.0;
    double meanAbsoluteOnsetErrorMs = 0.0;
    double p95OnsetErrorMs = 0.0;
};

[[nodiscard]] PhonemeEvaluationMetrics evaluatePhonemeFrames(
    const std::vector<PhonemeFrame>& reference,
    const std::vector<PhonemeFrame>& prediction,
    const PhonemeEvaluationOptions& options = {});

struct PhonemeFrameJsonLoadResult
{
    bool ok = false;
    std::string error;
    std::vector<PhonemeFrame> frames;
};

/// Parses an editable JSON array of frame objects:
/// [
///   {"arpabet":"K", "start":0.10, "end":0.15, "confidence":0.9},
///   {"arpabet":"AE", "estimatedOnsetSeconds":0.15, "estimatedEndSeconds":0.40}
/// ]
[[nodiscard]] PhonemeFrameJsonLoadResult parsePhonemeFrameLabelsJson(std::string_view json);

[[nodiscard]] PhonemeFrameJsonLoadResult loadPhonemeFrameLabelsJson(
    const std::filesystem::path& path);

[[nodiscard]] std::string phonemeEvaluationMetricsToJson(
    const PhonemeEvaluationMetrics& metrics);

[[nodiscard]] bool phonemeFrameIsConsonant(const PhonemeFrame& frame);

[[nodiscard]] double percentileSeconds(const std::vector<double>& valuesSeconds, double quantile);

} // namespace Voice2VocalSynth
