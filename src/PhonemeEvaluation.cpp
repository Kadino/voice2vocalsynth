#include "Voice2VocalSynth/PhonemeEvaluation.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Voice2VocalSynth
{
namespace
{

double overlapSeconds(const PhonemeFrame& a, const PhonemeFrame& b)
{
    return std::max(0.0,
                    std::min(a.estimatedEndSeconds, b.estimatedEndSeconds) -
                        std::max(a.estimatedOnsetSeconds, b.estimatedOnsetSeconds));
}

} // namespace

PhonemeEvaluationMetrics evaluatePhonemeFrames(
    const std::vector<PhonemeFrame>& reference,
    const std::vector<PhonemeFrame>& prediction,
    const PhonemeEvaluationOptions& options)
{
    PhonemeEvaluationMetrics metrics;
    metrics.referenceCount = reference.size();
    metrics.predictionCount = prediction.size();

    std::vector<bool> predictionUsed(prediction.size(), false);
    double onsetErrorSumSeconds = 0.0;

    for (const auto& ref : reference) {
        std::size_t bestIndex = prediction.size();
        double bestOverlap = 0.0;
        double bestOnsetError = std::numeric_limits<double>::infinity();

        for (std::size_t index = 0; index < prediction.size(); ++index) {
            if (predictionUsed[index] || prediction[index].arpabet != ref.arpabet) {
                continue;
            }

            const double overlap = overlapSeconds(ref, prediction[index]);
            const double onsetError = std::abs(prediction[index].estimatedOnsetSeconds -
                                               ref.estimatedOnsetSeconds);
            if (overlap >= options.minOverlapSeconds &&
                onsetError <= options.maxOnsetErrorSeconds &&
                (overlap > bestOverlap || (overlap == bestOverlap && onsetError < bestOnsetError))) {
                bestIndex = index;
                bestOverlap = overlap;
                bestOnsetError = onsetError;
            }
        }

        if (bestIndex == prediction.size()) {
            ++metrics.missedCount;
            continue;
        }

        predictionUsed[bestIndex] = true;
        ++metrics.matchedCount;
        onsetErrorSumSeconds += bestOnsetError;
    }

    metrics.falsePositiveCount = static_cast<std::size_t>(
        std::count(predictionUsed.begin(), predictionUsed.end(), false));
    metrics.substitutionOrTimingErrorCount = metrics.missedCount + metrics.falsePositiveCount;

    if (metrics.predictionCount > 0) {
        metrics.precision = static_cast<double>(metrics.matchedCount) /
                            static_cast<double>(metrics.predictionCount);
    }
    if (metrics.referenceCount > 0) {
        metrics.recall = static_cast<double>(metrics.matchedCount) /
                         static_cast<double>(metrics.referenceCount);
    }
    if (metrics.precision + metrics.recall > 0.0) {
        metrics.f1 = 2.0 * metrics.precision * metrics.recall /
                     (metrics.precision + metrics.recall);
    }
    if (metrics.matchedCount > 0) {
        metrics.meanAbsoluteOnsetErrorMs =
            (onsetErrorSumSeconds * 1000.0) / static_cast<double>(metrics.matchedCount);
    }

    return metrics;
}

} // namespace Voice2VocalSynth
