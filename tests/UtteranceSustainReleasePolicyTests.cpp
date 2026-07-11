#include <Voice2VocalSynth/UtteranceSustainReleasePolicy.h>

#include <cassert>
#include <cmath>
#include <iostream>

namespace
{
using namespace Voice2VocalSynth;

void shifts_release_with_inference_jitter()
{
    const auto settings = LatencyBudgetCalculator::presetSettings(LatencyPreset::Balanced);
    const auto breakdown = LatencyBudgetCalculator::calculate({}, settings);

    UtteranceSustainReleasePolicy policyNoJitter;
    policyNoJitter.on_speech_boundary({SpeechBoundaryKind::Onset, 0.0, 0.1F}, breakdown, 0.0);
    policyNoJitter.on_speech_boundary({SpeechBoundaryKind::End, 1.0, 0.0F}, breakdown, 0.0);

    UtteranceSustainReleasePolicy policyWithJitter;
    policyWithJitter.on_speech_boundary({SpeechBoundaryKind::Onset, 0.0, 0.1F}, breakdown, 0.0);
    policyWithJitter.on_speech_boundary({SpeechBoundaryKind::End, 1.0, 0.0F}, breakdown, 25.0);

    SustainReleaseCommand noJitterCmd;
    SustainReleaseCommand jitterCmd;
    const double noJitterRelease =
        PlaybackBoundaryMapper::analysisToPlaybackSeconds(1.0, breakdown, 0.0);
    const double jitterRelease =
        PlaybackBoundaryMapper::analysisToPlaybackSeconds(1.0, breakdown, 25.0);

    assert(policyNoJitter.try_pop_release(noJitterRelease + 0.001, noJitterCmd));
    assert(policyWithJitter.try_pop_release(jitterRelease + 0.001, jitterCmd));
    assert(jitterCmd.playback_time_seconds - noJitterCmd.playback_time_seconds > 0.024);
    assert(jitterCmd.playback_time_seconds - noJitterCmd.playback_time_seconds < 0.026);
}

void schedules_release_after_speech_end()
{
    const auto settings = LatencyBudgetCalculator::presetSettings(LatencyPreset::Balanced);
    const auto breakdown = LatencyBudgetCalculator::calculate({}, settings);

    UtteranceSustainReleasePolicy policy;
      policy.on_speech_boundary({SpeechBoundaryKind::Onset, 0.5, 0.1F}, breakdown, 0.0);
    assert(policy.sustaining());

    const double analysisEnd = 1.0;
    policy.on_speech_boundary({SpeechBoundaryKind::End, analysisEnd, 0.0F}, breakdown, 0.0);
    assert(!policy.sustaining());

    const double playbackEnd =
        PlaybackBoundaryMapper::analysisToPlaybackSeconds(analysisEnd, breakdown, 0.0);

    SustainReleaseCommand cmd;
    assert(!policy.try_pop_release(playbackEnd - 0.05, cmd));
    assert(policy.try_pop_release(playbackEnd + 0.001, cmd));
    assert(std::abs(cmd.playback_time_seconds - playbackEnd) < 1.0e-6);
    assert(std::abs(cmd.analysis_end_seconds - analysisEnd) < 1.0e-9);
}

} // namespace

int main()
{
    shifts_release_with_inference_jitter();
    schedules_release_after_speech_end();
    std::cout << "UtteranceSustainReleasePolicy tests passed\n";
    return 0;
}
