#pragma once

#include "Voice2VocalSynth/LatencyBudget.h"
#include "Voice2VocalSynth/PlaybackBoundaryMapper.h"
#include "Voice2VocalSynth/VoiceActivityDetector.h"

#include <deque>
#include <optional>

namespace Voice2VocalSynth
{

struct SustainReleaseCommand
{
    /// Playback timeline seconds when sustain should end and the trailing tail should play once.
    double playback_time_seconds = 0.0;
    /// Analysis-time `speech_end` that triggered this release.
    double analysis_end_seconds = 0.0;
};

/// Schedules sustain exit at render-time-aligned utterance end (`vadSynchronization.rendererInteraction`).
class UtteranceSustainReleasePolicy
{
public:
    void reset();

    /// Feed a VAD boundary; schedules release on `speech_end` using the latency mapper.
    void on_speech_boundary(const SpeechBoundaryEvent& boundary,
                            const LatencyBreakdown& breakdown,
                            double inference_jitter_ms);

    /// Returns a release command once `playback_now_seconds` reaches the scheduled release time.
    [[nodiscard]] bool try_pop_release(double playback_now_seconds, SustainReleaseCommand& out);

    [[nodiscard]] bool sustaining() const noexcept;
    [[nodiscard]] std::optional<double> pending_release_playback_seconds() const noexcept;

private:
    bool sustaining_ = false;
    std::optional<double> pending_release_playback_;
    double pending_analysis_end_ = 0.0;
    std::deque<SustainReleaseCommand> ready_;
};

} // namespace Voice2VocalSynth
