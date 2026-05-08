#pragma once

#include "Voice2VocalSynth/PitchTarget.h"

#include <cstddef>
#include <vector>

namespace Voice2VocalSynth
{

struct PitchHistoryOptions
{
    double windowMs = 750.0;
    double minimumConfidence = 0.6;
    std::size_t maxSamples = 256;
};

struct PitchObservation
{
    double timeSeconds = 0.0;
    double frequencyHz = 0.0;
    double confidence = 0.0;
    bool whispered = false;
};

struct PitchHistorySummary
{
    bool hasRecentMean = false;
    double recentMeanFrequencyHz = 0.0;
    std::size_t sampleCount = 0;
};

class RecentPitchTracker
{
public:
    RecentPitchTracker();
    explicit RecentPitchTracker(PitchHistoryOptions options);

    void addObservation(const PitchObservation& observation);
    void addInput(const PitchInput& input, double timeSeconds);
    void clear() noexcept;

    [[nodiscard]] PitchHistorySummary summarize(double nowSeconds) const;
    [[nodiscard]] PitchInput withRecentMean(PitchInput input, double nowSeconds) const;
    [[nodiscard]] const PitchHistoryOptions& options() const noexcept;
    [[nodiscard]] std::size_t storedSampleCount() const noexcept;

private:
    [[nodiscard]] bool shouldStore(const PitchObservation& observation) const noexcept;
    void prune(double nowSeconds);

    PitchHistoryOptions options_;
    std::vector<PitchObservation> observations_;
};

} // namespace Voice2VocalSynth
