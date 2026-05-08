#include <Voice2VocalSynth/PitchHistory.h>
#include <Voice2VocalSynth/PitchTarget.h>

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

void averagesRecentReliablePitchInHz()
{
    RecentPitchTracker tracker;
    tracker.addObservation({0.00, 220.0, 0.9});
    tracker.addObservation({0.25, 240.0, 0.8});
    tracker.addObservation({0.50, 260.0, 0.7});

    const auto summary = tracker.summarize(0.50);

    assert(summary.hasRecentMean);
    assert(summary.sampleCount == 3);
    assert(nearlyEqual(summary.recentMeanFrequencyHz, 240.0));
}

void ignoresLowConfidenceWhisperedAndInvalidPitch()
{
    RecentPitchTracker tracker;
    tracker.addObservation({0.00, 220.0, 0.9});
    tracker.addObservation({0.10, 440.0, 0.1});
    tracker.addObservation({0.20, 880.0, 0.9, true});
    tracker.addObservation({0.30, 0.0, 0.9});

    const auto summary = tracker.summarize(0.30);

    assert(summary.hasRecentMean);
    assert(summary.sampleCount == 1);
    assert(nearlyEqual(summary.recentMeanFrequencyHz, 220.0));
}

void dropsSamplesOutsideWindow()
{
    PitchHistoryOptions options;
    options.windowMs = 500.0;
    RecentPitchTracker tracker(options);
    tracker.addObservation({0.00, 100.0, 1.0});
    tracker.addObservation({0.60, 300.0, 1.0});

    const auto summary = tracker.summarize(0.60);

    assert(summary.hasRecentMean);
    assert(summary.sampleCount == 1);
    assert(nearlyEqual(summary.recentMeanFrequencyHz, 300.0));
    assert(tracker.storedSampleCount() == 1);
}

void enrichesPitchInputForTargetFallback()
{
    RecentPitchTracker tracker;
    tracker.addObservation({0.00, PitchTargetCalculator::midiToFrequency(64.0), 0.9});

    PitchInput input;
    input.frequencyHz = 120.0;
    input.confidence = 0.2;
    const auto enriched = tracker.withRecentMean(input, 0.25);

    PitchTargetOptions options;
    options.mode = PitchMode::SnapToNearestSemitone;
    options.minimumConfidence = 0.6;
    options.lowConfidenceBehavior = LowConfidencePitchBehavior::UseRecentMean;
    const PitchTargetCalculator calculator(options);

    const auto target = calculator.calculate(enriched);

    assert(target.usedLowConfidenceFallback);
    assert(!target.usedDefaultPitch);
    assert(target.displayMidiNote == 64);
    assert(target.displayNoteName == "E4");
}

void respectsMaxSamples()
{
    PitchHistoryOptions options;
    options.maxSamples = 2;
    RecentPitchTracker tracker(options);
    tracker.addObservation({0.0, 100.0, 1.0});
    tracker.addObservation({0.1, 200.0, 1.0});
    tracker.addObservation({0.2, 300.0, 1.0});

    const auto summary = tracker.summarize(0.2);

    assert(tracker.storedSampleCount() == 2);
    assert(summary.sampleCount == 2);
    assert(nearlyEqual(summary.recentMeanFrequencyHz, 250.0));
}

} // namespace

int main()
{
    averagesRecentReliablePitchInHz();
    ignoresLowConfidenceWhisperedAndInvalidPitch();
    dropsSamplesOutsideWindow();
    enrichesPitchInputForTargetFallback();
    respectsMaxSamples();

    std::cout << "PitchHistory tests passed\n";
    return 0;
}
