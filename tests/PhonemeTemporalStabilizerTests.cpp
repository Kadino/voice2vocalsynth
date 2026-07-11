#include <Voice2VocalSynth/PhonemeTemporalStabilizer.h>

#include <cassert>
#include <iostream>
#include <vector>

namespace
{
using namespace Voice2VocalSynth;

void silence_after_vowel_emits_one_segment()
{
    PhonemeTemporalStabilizerOptions opt;
    opt.min_segment_seconds = 0.04;
    opt.silence_finalize_seconds = 0.05;
    opt.candidate_stable_seconds = 0.04;
    PhonemeTemporalStabilizer stab(opt);

    for (int i = 0; i < 8; ++i) {
        stab.observe({0.01 * static_cast<double>(i), "AH", 0.9F});
    }
    PhonemeFrame f;
    assert(!stab.try_pop_committed(f));

    for (int k = 0; k < 10; ++k) {
        stab.observe({0.08 + 0.01 * static_cast<double>(k), "", 0.0F});
    }
    assert(stab.try_pop_committed(f));
    assert(f.arpabet == "AH");
    assert(f.isVowel);
    assert(f.estimatedEndSeconds - f.estimatedOnsetSeconds >= opt.min_segment_seconds - 1.0e-6);
    assert(!stab.try_pop_committed(f));
}

void stable_competitor_produces_two_segments()
{
    PhonemeTemporalStabilizerOptions opt;
    opt.min_segment_seconds = 0.03;
    opt.candidate_stable_seconds = 0.08;
    opt.silence_finalize_seconds = 0.06;
    opt.min_observation_confidence = 0.4F;
    opt.hysteresis_confidence_delta = 0.05F;
    PhonemeTemporalStabilizer stab(opt);

    for (int i = 0; i < 6; ++i) {
        stab.observe({0.01 * static_cast<double>(i), "AH", 0.85F});
    }
    for (int i = 0; i < 12; ++i) {
        stab.observe({0.06 + 0.01 * static_cast<double>(i), "EH", 0.85F});
    }
    for (int k = 0; k < 10; ++k) {
        stab.observe({0.2 + 0.01 * static_cast<double>(k), "", 0.0F});
    }

    PhonemeFrame a;
    PhonemeFrame b;
    assert(stab.try_pop_committed(a));
    assert(stab.try_pop_committed(b));
    assert(a.arpabet == "AH");
    assert(b.arpabet == "EH");
    assert(b.estimatedOnsetSeconds >= a.estimatedEndSeconds - 1.0e-9);
}

void short_blip_dropped()
{
    PhonemeTemporalStabilizerOptions opt;
    opt.min_segment_seconds = 0.08;
    opt.silence_finalize_seconds = 0.05;
    PhonemeTemporalStabilizer stab(opt);

    stab.observe({0.0, "IH", 0.9F});
    stab.observe({0.02, "IH", 0.9F});
    stab.observe({0.03, "", 0.0F});
    stab.observe({0.12, "", 0.0F});

    PhonemeFrame f;
    assert(!stab.try_pop_committed(f));
}

void rapid_label_flicker_keeps_first_stable_segment()
{
    PhonemeTemporalStabilizerOptions opt;
    opt.min_segment_seconds = 0.04;
    opt.candidate_stable_seconds = 0.06;
    opt.silence_finalize_seconds = 0.05;
    PhonemeTemporalStabilizer stab(opt);

    for (int i = 0; i < 6; ++i) {
        stab.observe({0.01 * static_cast<double>(i), "AH", 0.85F});
    }
    stab.observe({0.06, "EH", 0.85F});
    stab.observe({0.07, "AH", 0.85F});
    for (int k = 0; k < 8; ++k) {
        stab.observe({0.08 + 0.01 * static_cast<double>(k), "", 0.0F});
    }

    PhonemeFrame frame;
    assert(stab.try_pop_committed(frame));
    assert(frame.arpabet == "AH");
    assert(!stab.try_pop_committed(frame));
}

void reset_clears_pending_segments()
{
    PhonemeTemporalStabilizerOptions opt;
    opt.min_segment_seconds = 0.04;
    opt.silence_finalize_seconds = 0.05;
    PhonemeTemporalStabilizer stab(opt);

    for (int i = 0; i < 8; ++i) {
        stab.observe({0.01 * static_cast<double>(i), "IH", 0.9F});
    }
    stab.reset();
    for (int k = 0; k < 10; ++k) {
        stab.observe({0.2 + 0.01 * static_cast<double>(k), "", 0.0F});
    }

    PhonemeFrame frame;
    assert(!stab.try_pop_committed(frame));
}

void low_confidence_frames_ignored()
{
    PhonemeTemporalStabilizerOptions opt;
    opt.min_segment_seconds = 0.04;
    opt.min_observation_confidence = 0.5F;
    opt.silence_finalize_seconds = 0.05;
    PhonemeTemporalStabilizer stab(opt);

    for (int i = 0; i < 8; ++i) {
        stab.observe({0.01 * static_cast<double>(i), "AH", 0.2F});
    }
    for (int k = 0; k < 10; ++k) {
        stab.observe({0.08 + 0.01 * static_cast<double>(k), "", 0.0F});
    }

    PhonemeFrame frame;
    assert(!stab.try_pop_committed(frame));
}

void unvoiced_consonant_marked_not_voiced()
{
    PhonemeTemporalStabilizerOptions opt;
    opt.min_segment_seconds = 0.04;
    opt.silence_finalize_seconds = 0.05;
    PhonemeTemporalStabilizer stab(opt);

    for (int i = 0; i < 8; ++i) {
        stab.observe({0.01 * static_cast<double>(i), "T", 0.9F});
    }
    for (int k = 0; k < 10; ++k) {
        stab.observe({0.08 + 0.01 * static_cast<double>(k), "", 0.0F});
    }

    PhonemeFrame frame;
    assert(stab.try_pop_committed(frame));
    assert(frame.arpabet == "T");
    assert(frame.isConsonant);
    assert(!frame.isVoiced);
}

void out_of_order_observation_resets_state()
{
    PhonemeTemporalStabilizerOptions opt;
    opt.min_segment_seconds = 0.04;
    opt.silence_finalize_seconds = 0.05;
    PhonemeTemporalStabilizer stab(opt);

    for (int i = 0; i < 8; ++i) {
        stab.observe({0.01 * static_cast<double>(i), "AH", 0.9F});
    }
    stab.observe({0.0, "EH", 0.9F});
    for (int k = 0; k < 10; ++k) {
        stab.observe({0.2 + 0.01 * static_cast<double>(k), "", 0.0F});
    }

    PhonemeFrame frame;
    assert(!stab.try_pop_committed(frame));
}

void weak_competitor_blocked_by_hysteresis()
{
    PhonemeTemporalStabilizerOptions opt;
    opt.min_segment_seconds = 0.04;
    opt.candidate_stable_seconds = 0.08;
    opt.silence_finalize_seconds = 0.05;
    opt.min_observation_confidence = 0.5F;
    opt.hysteresis_confidence_delta = 0.2F;
    PhonemeTemporalStabilizer stab(opt);

    for (int i = 0; i < 8; ++i) {
        stab.observe({0.01 * static_cast<double>(i), "AH", 0.9F});
    }
    for (int i = 0; i < 12; ++i) {
        stab.observe({0.08 + 0.01 * static_cast<double>(i), "EH", 0.55F});
    }
    for (int k = 0; k < 10; ++k) {
        stab.observe({0.2 + 0.01 * static_cast<double>(k), "", 0.0F});
    }

    PhonemeFrame frame;
    assert(stab.try_pop_committed(frame));
    assert(frame.arpabet == "AH");
    assert(!stab.try_pop_committed(frame));
}

} // namespace

int main()
{
    silence_after_vowel_emits_one_segment();
    stable_competitor_produces_two_segments();
    short_blip_dropped();
    rapid_label_flicker_keeps_first_stable_segment();
    reset_clears_pending_segments();
    low_confidence_frames_ignored();
    unvoiced_consonant_marked_not_voiced();
    out_of_order_observation_resets_state();
    weak_competitor_blocked_by_hysteresis();
    std::cout << "PhonemeTemporalStabilizer tests passed\n";
    return 0;
}
