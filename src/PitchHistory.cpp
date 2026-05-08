#include "Voice2VocalSynth/PitchHistory.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace Voice2VocalSynth
{
namespace
{

bool isUsableFrequency(double frequencyHz)
{
    return std::isfinite(frequencyHz) && frequencyHz > 0.0;
}

} // namespace

RecentPitchTracker::RecentPitchTracker()
    : RecentPitchTracker(PitchHistoryOptions{})
{
}

RecentPitchTracker::RecentPitchTracker(PitchHistoryOptions options)
    : options_(options)
{
}

void RecentPitchTracker::addObservation(const PitchObservation& observation)
{
    prune(observation.timeSeconds);

    if (!shouldStore(observation)) {
        return;
    }

    observations_.push_back(observation);
    if (observations_.size() > options_.maxSamples) {
        observations_.erase(observations_.begin(),
                            observations_.begin() +
                                static_cast<std::ptrdiff_t>(observations_.size() - options_.maxSamples));
    }
}

void RecentPitchTracker::addInput(const PitchInput& input, double timeSeconds)
{
    addObservation({timeSeconds, input.frequencyHz, input.confidence, input.whispered});
}

void RecentPitchTracker::clear() noexcept
{
    observations_.clear();
}

PitchHistorySummary RecentPitchTracker::summarize(double nowSeconds) const
{
    const auto windowSeconds = options_.windowMs / 1000.0;
    double sum = 0.0;
    std::size_t count = 0;

    for (const auto& observation : observations_) {
        if (observation.timeSeconds > nowSeconds) {
            continue;
        }
        if (nowSeconds - observation.timeSeconds > windowSeconds) {
            continue;
        }

        sum += observation.frequencyHz;
        ++count;
    }

    PitchHistorySummary summary;
    summary.sampleCount = count;
    if (count > 0) {
        summary.hasRecentMean = true;
        summary.recentMeanFrequencyHz = sum / static_cast<double>(count);
    }
    return summary;
}

PitchInput RecentPitchTracker::withRecentMean(PitchInput input, double nowSeconds) const
{
    const auto summary = summarize(nowSeconds);
    if (summary.hasRecentMean) {
        input.recentMeanFrequencyHz = summary.recentMeanFrequencyHz;
    }
    return input;
}

const PitchHistoryOptions& RecentPitchTracker::options() const noexcept
{
    return options_;
}

std::size_t RecentPitchTracker::storedSampleCount() const noexcept
{
    return observations_.size();
}

bool RecentPitchTracker::shouldStore(const PitchObservation& observation) const noexcept
{
    return !observation.whispered &&
           observation.confidence >= options_.minimumConfidence &&
           isUsableFrequency(observation.frequencyHz);
}

void RecentPitchTracker::prune(double nowSeconds)
{
    const auto windowSeconds = options_.windowMs / 1000.0;
    observations_.erase(
        std::remove_if(observations_.begin(),
                       observations_.end(),
                       [nowSeconds, windowSeconds](const auto& observation) {
                           return observation.timeSeconds <= nowSeconds &&
                                  nowSeconds - observation.timeSeconds > windowSeconds;
                       }),
        observations_.end());
}

} // namespace Voice2VocalSynth
