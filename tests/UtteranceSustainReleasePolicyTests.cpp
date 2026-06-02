#include <Voice2VocalSynth/UtteranceSustainReleasePolicy.h>

#include <cassert>
#include <cmath>
#include <iostream>

namespace
{
using namespace Voice2VocalSynth;

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
    schedules_release_after_speech_end();
    std::cout << "UtteranceSustainReleasePolicy tests passed\n";
    return 0;
}
