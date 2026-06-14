#pragma once

#include "Voice2VocalSynth/PhonemeFrame.h"

#include <cstddef>
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
    double precision = 0.0;
    double recall = 0.0;
    double f1 = 0.0;
    double meanAbsoluteOnsetErrorMs = 0.0;
};

[[nodiscard]] PhonemeEvaluationMetrics evaluatePhonemeFrames(
    const std::vector<PhonemeFrame>& reference,
    const std::vector<PhonemeFrame>& prediction,
    const PhonemeEvaluationOptions& options = {});

} // namespace Voice2VocalSynth
