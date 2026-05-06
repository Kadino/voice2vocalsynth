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

void convertsFrequencyToMidiAndBack()
{
    assert(nearlyEqual(PitchTargetCalculator::frequencyToMidi(440.0), 69.0));
    assert(nearlyEqual(PitchTargetCalculator::midiToFrequency(69.0), 440.0));
}

void followsInputPitchByDefault()
{
    const PitchTargetCalculator calculator;

    const auto target = calculator.calculate({442.0, 1.0});

    assert(nearlyEqual(target.targetFrequencyHz, 442.0));
    assert(target.displayMidiNote == 69);
    assert(target.displayNoteName == "A4");
    assert(!target.snapped);
}

void snapsToNearestSemitone()
{
    PitchTargetOptions options;
    options.mode = PitchMode::SnapToNearestSemitone;
    const PitchTargetCalculator calculator(options);

    const auto target = calculator.calculate({445.0, 1.0});

    assert(nearlyEqual(target.targetFrequencyHz, 440.0));
    assert(target.displayNoteName == "A4");
    assert(target.snapped);
}

void snapsToSelectedKey()
{
    PitchTargetOptions options;
    options.mode = PitchMode::SnapToKey;
    options.keyRootPitchClass = 0;
    options.scale = ScaleType::Major;
    const PitchTargetCalculator calculator(options);

    const auto target = calculator.calculate({PitchTargetCalculator::midiToFrequency(62.8), 1.0});

    assert(target.displayMidiNote == 62);
    assert(target.displayNoteName == "D4");
    assert(nearlyEqual(target.targetFrequencyHz, PitchTargetCalculator::midiToFrequency(62.0)));
}

void appliesOctaveShiftAfterSnapping()
{
    PitchTargetOptions options;
    options.mode = PitchMode::SnapToNearestSemitone;
    options.octaveShift = 1;
    const PitchTargetCalculator calculator(options);

    const auto target = calculator.calculate({PitchTargetCalculator::midiToFrequency(60.2), 1.0});

    assert(target.displayMidiNote == 72);
    assert(target.displayNoteName == "C5");
    assert(nearlyEqual(target.targetFrequencyHz, PitchTargetCalculator::midiToFrequency(72.0)));
}

void usesFixedDefaultPitch()
{
    PitchTargetOptions options;
    options.mode = PitchMode::FixedDefault;
    options.defaultFrequencyHz = PitchTargetCalculator::midiToFrequency(65.0);
    options.octaveShift = -1;
    const PitchTargetCalculator calculator(options);

    const auto target = calculator.calculate({440.0, 1.0});

    assert(target.usedDefaultPitch);
    assert(target.displayMidiNote == 65);
    assert(target.displayNoteName == "F4");
    assert(nearlyEqual(target.targetFrequencyHz, PitchTargetCalculator::midiToFrequency(65.0)));
}

void lowConfidenceUsesRecentMean()
{
    PitchTargetOptions options;
    options.mode = PitchMode::SnapToNearestSemitone;
    options.minimumConfidence = 0.6;
    options.lowConfidenceBehavior = LowConfidencePitchBehavior::UseRecentMean;
    const PitchTargetCalculator calculator(options);

    PitchInput input;
    input.frequencyHz = 220.0;
    input.confidence = 0.2;
    input.recentMeanFrequencyHz = PitchTargetCalculator::midiToFrequency(64.0);
    const auto target = calculator.calculate(input);

    assert(target.usedLowConfidenceFallback);
    assert(!target.usedDefaultPitch);
    assert(target.displayMidiNote == 64);
    assert(target.displayNoteName == "E4");
}

void whisperedLowConfidenceUsesVoicebankDefault()
{
    PitchTargetOptions options;
    options.mode = PitchMode::FollowInput;
    options.defaultFrequencyHz = PitchTargetCalculator::midiToFrequency(60.0);
    options.minimumConfidence = 0.6;
    options.octaveShift = 2;
    options.lowConfidenceBehavior = LowConfidencePitchBehavior::UseRecentMean;
    const PitchTargetCalculator calculator(options);

    PitchInput input;
    input.frequencyHz = 220.0;
    input.confidence = 0.1;
    input.recentMeanFrequencyHz = PitchTargetCalculator::midiToFrequency(67.0);
    input.whispered = true;
    const auto target = calculator.calculate(input);

    assert(target.usedLowConfidenceFallback);
    assert(target.usedDefaultPitch);
    assert(target.displayMidiNote == 60);
    assert(target.displayNoteName == "C4");
}

void lowConfidenceDefaultBypassesSnapAndOctaveShift()
{
    PitchTargetOptions options;
    options.mode = PitchMode::SnapToNearestSemitone;
    options.defaultFrequencyHz = PitchTargetCalculator::midiToFrequency(60.4);
    options.minimumConfidence = 0.6;
    options.octaveShift = 1;
    options.lowConfidenceBehavior = LowConfidencePitchBehavior::UseDefaultPitch;
    const PitchTargetCalculator calculator(options);

    PitchInput input;
    input.frequencyHz = PitchTargetCalculator::midiToFrequency(69.0);
    input.confidence = 0.1;
    const auto target = calculator.calculate(input);

    assert(target.usedLowConfidenceFallback);
    assert(target.usedDefaultPitch);
    assert(!target.snapped);
    assert(nearlyEqual(target.targetMidi, 60.4));
    assert(nearlyEqual(target.targetFrequencyHz, PitchTargetCalculator::midiToFrequency(60.4)));
}

void snapStrengthBlendsInMidiSpace()
{
    PitchTargetOptions options;
    options.mode = PitchMode::SnapToNearestSemitone;
    options.snapStrength = 0.5;
    const PitchTargetCalculator calculator(options);

    const auto inputMidi = 69.4;
    const auto target = calculator.calculate({PitchTargetCalculator::midiToFrequency(inputMidi), 1.0});

    assert(nearlyEqual(PitchTargetCalculator::frequencyToMidi(target.targetFrequencyHz), 69.2));
}

} // namespace

int main()
{
    convertsFrequencyToMidiAndBack();
    followsInputPitchByDefault();
    snapsToNearestSemitone();
    snapsToSelectedKey();
    appliesOctaveShiftAfterSnapping();
    usesFixedDefaultPitch();
    lowConfidenceUsesRecentMean();
    whisperedLowConfidenceUsesVoicebankDefault();
    lowConfidenceDefaultBypassesSnapAndOctaveShift();
    snapStrengthBlendsInMidiSpace();

    std::cout << "PitchTarget tests passed\n";
    return 0;
}
