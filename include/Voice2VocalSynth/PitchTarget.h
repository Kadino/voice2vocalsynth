#pragma once

#include <string>
#include <vector>

namespace Voice2VocalSynth
{

enum class PitchMode
{
    FollowInput,
    SnapToNearestSemitone,
    SnapToKey,
    FixedDefault
};

enum class ScaleType
{
    Major,
    NaturalMinor,
    MajorPentatonic,
    MinorPentatonic,
    Chromatic
};

enum class LowConfidencePitchBehavior
{
    UseRecentMean,
    UseDefaultPitch
};

struct PitchInput
{
    double frequencyHz = 0.0;
    double confidence = 0.0;
    double recentMeanFrequencyHz = 0.0;
    bool whispered = false;
};

struct PitchTargetOptions
{
    PitchMode mode = PitchMode::FollowInput;
    ScaleType scale = ScaleType::Major;
    int keyRootPitchClass = 0;
    int octaveShift = 0;
    double defaultFrequencyHz = 261.6255653005986; // C4 in 12-tone equal temperament.
    double minimumConfidence = 0.6;
    double snapStrength = 1.0;
    LowConfidencePitchBehavior lowConfidenceBehavior = LowConfidencePitchBehavior::UseRecentMean;
};

struct PitchTarget
{
    double sourceFrequencyHz = 0.0;
    double targetFrequencyHz = 0.0;
    double targetMidi = 0.0;
    int displayMidiNote = 0;
    std::string displayNoteName;
    bool usedLowConfidenceFallback = false;
    bool usedDefaultPitch = false;
    bool snapped = false;
};

class PitchTargetCalculator
{
public:
    PitchTargetCalculator() = default;
    explicit PitchTargetCalculator(PitchTargetOptions options);

    [[nodiscard]] PitchTarget calculate(const PitchInput& input) const;
    [[nodiscard]] const PitchTargetOptions& options() const noexcept;

    [[nodiscard]] static double frequencyToMidi(double frequencyHz);
    [[nodiscard]] static double midiToFrequency(double midiNote);
    [[nodiscard]] static std::string midiNoteName(int midiNote);
    [[nodiscard]] static std::vector<int> scaleIntervals(ScaleType scale);

private:
    [[nodiscard]] double chooseSourceFrequency(const PitchInput& input, PitchTarget& target) const;
    [[nodiscard]] double snapMidiToNearestSemitone(double midiNote) const;
    [[nodiscard]] double snapMidiToKey(double midiNote) const;

    PitchTargetOptions options_;
};

} // namespace Voice2VocalSynth
