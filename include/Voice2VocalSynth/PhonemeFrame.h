#pragma once

#include <string>

namespace Voice2VocalSynth
{

/// Single time-slice phoneme hypothesis aligned with `voice2vocalsynth.spec.md`
/// `phonemeDetection.outputStructSpec` (PhonemeFrame).
struct PhonemeFrame
{
    std::string arpabet;
    float confidence = 0.0F;
    double estimatedOnsetSeconds = 0.0;
    double estimatedEndSeconds = 0.0;
    bool isVoiced = false;
    bool isConsonant = false;
    bool isVowel = false;
};

} // namespace Voice2VocalSynth
