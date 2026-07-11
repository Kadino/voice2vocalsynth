#include <Voice2VocalSynth/PhonemeBakeoff.h>
#include <Voice2VocalSynth/PhonemeEvaluation.h>
#include <Voice2VocalSynth/PhonemeTemporalStabilizer.h>

#include <cassert>
#include <iostream>

namespace
{
using namespace Voice2VocalSynth;

void stabilizerCommittedFramesClassifyVowels()
{
    PhonemeTemporalStabilizerOptions opt;
    opt.min_segment_seconds = 0.04;
    opt.silence_finalize_seconds = 0.05;
    PhonemeTemporalStabilizer stabilizer(opt);

    for (int i = 0; i < 8; ++i) {
        stabilizer.observe({0.01 * static_cast<double>(i), "AH", 0.9F});
    }
    for (int k = 0; k < 10; ++k) {
        stabilizer.observe({0.08 + 0.01 * static_cast<double>(k), "", 0.0F});
    }

    PhonemeFrame frame;
    assert(stabilizer.try_pop_committed(frame));
    assert(frame.arpabet == "AH");
    assert(frame.isVowel);
    assert(!frame.isConsonant);
    assert(frame.isVoiced);
}

void stabilizerCommittedFramesClassifyUnvoicedConsonants()
{
    for (const char* token : {"P", "T", "K", "CH", "F", "TH", "S", "SH", "HH"}) {
        PhonemeTemporalStabilizerOptions opt;
        opt.min_segment_seconds = 0.04;
        opt.silence_finalize_seconds = 0.05;
        PhonemeTemporalStabilizer stabilizer(opt);

        for (int i = 0; i < 8; ++i) {
            stabilizer.observe({0.01 * static_cast<double>(i), token, 0.9F});
        }
        for (int k = 0; k < 10; ++k) {
            stabilizer.observe({0.08 + 0.01 * static_cast<double>(k), "", 0.0F});
        }

        PhonemeFrame frame;
        assert(stabilizer.try_pop_committed(frame));
        assert(frame.arpabet == token);
        assert(!frame.isVowel);
        assert(frame.isConsonant);
        assert(!frame.isVoiced);
    }
}

void stabilizerCommittedFramesClassifyVoicedConsonants()
{
    for (const char* token : {"M", "N", "L", "R", "B", "D", "G", "V", "Z"}) {
        PhonemeTemporalStabilizerOptions opt;
        opt.min_segment_seconds = 0.04;
        opt.silence_finalize_seconds = 0.05;
        PhonemeTemporalStabilizer stabilizer(opt);

        for (int i = 0; i < 8; ++i) {
            stabilizer.observe({0.01 * static_cast<double>(i), token, 0.9F});
        }
        for (int k = 0; k < 10; ++k) {
            stabilizer.observe({0.08 + 0.01 * static_cast<double>(k), "", 0.0F});
        }

        PhonemeFrame frame;
        assert(stabilizer.try_pop_committed(frame));
        assert(frame.arpabet == token);
        assert(!frame.isVowel);
        assert(frame.isConsonant);
        assert(frame.isVoiced);
    }
}

void observationToFramePopulatesSpecFields()
{
    PhonemeTemporalObservation observation;
    observation.stream_time_seconds = 0.5;
    observation.arpabet = "EH";
    observation.confidence = 0.75F;

    const auto frame = phonemeObservationToFrame(observation, 0.04);
    assert(frame.arpabet == "EH");
    assert(frame.confidence == 0.75F);
    assert(frame.estimatedOnsetSeconds == 0.46);
    assert(frame.estimatedEndSeconds == 0.5);
    assert(frame.isVowel);
    assert(!frame.isConsonant);
    assert(frame.isVoiced);
}

void phonemeFrameIsConsonantUsesExplicitFlagsFirst()
{
    PhonemeFrame explicitConsonant;
    explicitConsonant.arpabet = "AH";
    explicitConsonant.isConsonant = true;
    explicitConsonant.isVowel = false;
    assert(phonemeFrameIsConsonant(explicitConsonant));

    PhonemeFrame explicitVowel;
    explicitVowel.arpabet = "K";
    explicitVowel.isVowel = true;
    explicitVowel.isConsonant = false;
    assert(!phonemeFrameIsConsonant(explicitVowel));
}

void phonemeFrameIsConsonantInfersFromArpabet()
{
    PhonemeFrame inferred;
    inferred.arpabet = "K";
    assert(phonemeFrameIsConsonant(inferred));

    inferred.arpabet = "IY";
    assert(!phonemeFrameIsConsonant(inferred));
}

void labelJsonRoundTripsVoicingFlags()
{
    const auto result = parsePhonemeFrameLabelsJson(
        "[\n"
        "  {\"arpabet\":\"P\", \"start\":0.0, \"end\":0.05, \"confidence\":0.9, "
        "\"isVoiced\":false, \"isConsonant\":true, \"isVowel\":false},\n"
        "  {\"arpabet\":\"AA\", \"start\":0.05, \"end\":0.20, \"confidence\":0.95, "
        "\"isVoiced\":true, \"isConsonant\":false, \"isVowel\":true}\n"
        "]\n");
    assert(result.ok);
    assert(result.frames.size() == 2);
    assert(result.frames[0].arpabet == "P");
    assert(!result.frames[0].isVoiced);
    assert(result.frames[0].isConsonant);
    assert(!result.frames[0].isVowel);
    assert(result.frames[1].arpabet == "AA");
    assert(result.frames[1].isVoiced);
    assert(!result.frames[1].isConsonant);
    assert(result.frames[1].isVowel);
}

} // namespace

int main()
{
    stabilizerCommittedFramesClassifyVowels();
    stabilizerCommittedFramesClassifyUnvoicedConsonants();
    stabilizerCommittedFramesClassifyVoicedConsonants();
    observationToFramePopulatesSpecFields();
    phonemeFrameIsConsonantUsesExplicitFlagsFirst();
    phonemeFrameIsConsonantInfersFromArpabet();
    labelJsonRoundTripsVoicingFlags();

    std::cout << "PhonemeFrameContract tests passed\n";
    return 0;
}
