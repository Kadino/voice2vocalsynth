#include "Voice2VocalSynth/PitchTarget.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Voice2VocalSynth
{
namespace
{

constexpr std::array<const char*, 12> kNoteNames = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
};

bool isUsableFrequency(double frequencyHz)
{
    return std::isfinite(frequencyHz) && frequencyHz > 0.0;
}

double clampSnapStrength(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

int positivePitchClass(int midiNote)
{
    const auto pitchClass = midiNote % 12;
    return pitchClass < 0 ? pitchClass + 12 : pitchClass;
}

std::vector<int> scaleIntervalsFor(ScaleType scale)
{
    switch (scale) {
        case ScaleType::Chromatic:
            return {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
        case ScaleType::Major:
            return {0, 2, 4, 5, 7, 9, 11};
        case ScaleType::NaturalMinor:
            return {0, 2, 3, 5, 7, 8, 10};
        case ScaleType::MajorPentatonic:
            return {0, 2, 4, 7, 9};
        case ScaleType::MinorPentatonic:
            return {0, 3, 5, 7, 10};
    }

    return {0};
}

bool isPitchClassInScale(int pitchClass, int keyRootPitchClass, ScaleType scale)
{
    const auto root = positivePitchClass(keyRootPitchClass);
    for (const auto interval : scaleIntervalsFor(scale)) {
        if (positivePitchClass(root + interval) == positivePitchClass(pitchClass)) {
            return true;
        }
    }

    return false;
}

int snapMidiToScale(double midi, int keyRootPitchClass, ScaleType scale)
{
    const auto nearest = static_cast<int>(std::round(midi));
    auto bestMidi = nearest;
    auto bestDistance = std::numeric_limits<double>::infinity();

    for (int candidate = nearest - 12; candidate <= nearest + 12; ++candidate) {
        if (!isPitchClassInScale(candidate, keyRootPitchClass, scale)) {
            continue;
        }

        const auto distance = std::abs(static_cast<double>(candidate) - midi);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestMidi = candidate;
        }
    }

    return bestMidi;
}

double choosePitchSourceFrequency(const PitchInput& input,
                                  const PitchTargetOptions& options,
                                  PitchTarget& target)
{
    target.sourceFrequencyHz = isUsableFrequency(input.frequencyHz) ? input.frequencyHz : 0.0;

    const auto confidenceIsLow = input.confidence < options.minimumConfidence;
    if (!confidenceIsLow && isUsableFrequency(input.frequencyHz)) {
        return input.frequencyHz;
    }

    target.usedLowConfidenceFallback = true;

    if (input.whispered || options.lowConfidenceBehavior == LowConfidencePitchBehavior::UseDefaultPitch) {
        target.usedDefaultPitch = true;
        return options.defaultFrequencyHz;
    }

    if (options.lowConfidenceBehavior == LowConfidencePitchBehavior::UseRecentMean &&
        isUsableFrequency(input.recentMeanFrequencyHz)) {
        return input.recentMeanFrequencyHz;
    }

    target.usedDefaultPitch = true;
    return options.defaultFrequencyHz;
}

} // namespace

PitchTargetCalculator::PitchTargetCalculator(PitchTargetOptions options)
    : options_(options)
{
}

PitchTarget PitchTargetCalculator::calculate(const PitchInput& input) const
{
    PitchTarget target;

    if (!isUsableFrequency(options_.defaultFrequencyHz)) {
        throw std::invalid_argument("defaultFrequencyHz must be positive and finite");
    }

    const auto workingFrequencyHz = choosePitchSourceFrequency(input, options_, target);
    auto workingMidi = frequencyToMidi(workingFrequencyHz);

    if (options_.mode == PitchMode::FixedDefault) {
        workingMidi = frequencyToMidi(options_.defaultFrequencyHz);
        target.usedDefaultPitch = true;
    }

    if (!target.usedDefaultPitch) {
        switch (options_.mode) {
            case PitchMode::FollowInput:
                target.snapped = false;
                break;
            case PitchMode::SnapToNearestSemitone: {
                const auto snappedMidi = snapMidiToNearestSemitone(workingMidi);
                workingMidi += (snappedMidi - workingMidi) * clampSnapStrength(options_.snapStrength);
                target.snapped = options_.snapStrength > 0.0;
                break;
            }
            case PitchMode::SnapToKey: {
                const auto snappedMidi = snapMidiToKey(workingMidi);
                workingMidi += (snappedMidi - workingMidi) * clampSnapStrength(options_.snapStrength);
                target.snapped = options_.snapStrength > 0.0;
                break;
            }
            case PitchMode::FixedDefault:
                target.snapped = false;
                break;
        }

        workingMidi += static_cast<double>(options_.octaveShift) * 12.0;
    } else {
        target.snapped = false;
    }

    target.targetMidi = workingMidi;
    target.targetFrequencyHz = midiToFrequency(workingMidi);
    target.displayMidiNote = static_cast<int>(std::round(workingMidi));
    target.displayNoteName = midiNoteName(target.displayMidiNote);
    return target;
}

const PitchTargetOptions& PitchTargetCalculator::options() const noexcept
{
    return options_;
}

double PitchTargetCalculator::midiToFrequency(double midiNote)
{
    return 440.0 * std::pow(2.0, (midiNote - 69.0) / 12.0);
}

double PitchTargetCalculator::frequencyToMidi(double frequencyHz)
{
    if (!isUsableFrequency(frequencyHz)) {
        throw std::invalid_argument("frequencyToMidi requires a positive finite frequency");
    }

    return 69.0 + 12.0 * std::log2(frequencyHz / 440.0);
}

std::string PitchTargetCalculator::midiNoteName(int midiNote)
{
    const auto pitchClass = positivePitchClass(midiNote);
    const auto octave = midiNote / 12 - 1;
    return std::string(kNoteNames[static_cast<std::size_t>(pitchClass)]) + std::to_string(octave);
}

std::vector<int> PitchTargetCalculator::scaleIntervals(ScaleType scale)
{
    return scaleIntervalsFor(scale);
}

double PitchTargetCalculator::chooseSourceFrequency(const PitchInput& input, PitchTarget& target) const
{
    return choosePitchSourceFrequency(input, options_, target);
}

double PitchTargetCalculator::snapMidiToNearestSemitone(double midiNote) const
{
    return std::round(midiNote);
}

double PitchTargetCalculator::snapMidiToKey(double midiNote) const
{
    return static_cast<double>(snapMidiToScale(midiNote, options_.keyRootPitchClass, options_.scale));
}

} // namespace Voice2VocalSynth
