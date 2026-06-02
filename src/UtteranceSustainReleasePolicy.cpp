#include "Voice2VocalSynth/UtteranceSustainReleasePolicy.h"

namespace Voice2VocalSynth
{

void UtteranceSustainReleasePolicy::reset()
{
    sustaining_ = false;
    pending_release_playback_.reset();
    pending_analysis_end_ = 0.0;
    ready_.clear();
}

bool UtteranceSustainReleasePolicy::sustaining() const noexcept
{
    return sustaining_;
}

std::optional<double> UtteranceSustainReleasePolicy::pending_release_playback_seconds() const noexcept
{
    return pending_release_playback_;
}

void UtteranceSustainReleasePolicy::on_speech_boundary(const SpeechBoundaryEvent& boundary,
                                                       const LatencyBreakdown& breakdown,
                                                       double inference_jitter_ms)
{
    if (boundary.kind == SpeechBoundaryKind::Onset) {
        sustaining_ = true;
        pending_release_playback_.reset();
        return;
    }

    if (boundary.kind != SpeechBoundaryKind::End) {
        return;
    }

    sustaining_ = false;
    const double playbackEnd = PlaybackBoundaryMapper::analysisToPlaybackSeconds(
        boundary.stream_time_seconds, breakdown, inference_jitter_ms);
    pending_release_playback_ = playbackEnd;
    pending_analysis_end_ = boundary.stream_time_seconds;

    SustainReleaseCommand cmd;
    cmd.playback_time_seconds = playbackEnd;
    cmd.analysis_end_seconds = boundary.stream_time_seconds;
    ready_.push_back(cmd);
}

bool UtteranceSustainReleasePolicy::try_pop_release(double playback_now_seconds,
                                                    SustainReleaseCommand& out)
{
    if (ready_.empty()) {
        return false;
    }

    const auto& front = ready_.front();
    if (playback_now_seconds + 1.0e-9 < front.playback_time_seconds) {
        return false;
    }

    out = front;
    ready_.pop_front();
    if (pending_release_playback_ && std::abs(*pending_release_playback_ - out.playback_time_seconds) < 1.0e-9) {
        pending_release_playback_.reset();
    }
    return true;
}

} // namespace Voice2VocalSynth
