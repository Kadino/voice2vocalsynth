#pragma once

#include <cmath>
#include <deque>
#include <string>

namespace Voice2VocalSynth
{

enum class SpeechBoundaryKind
{
    Onset,
    End
};

struct SpeechBoundaryEvent
{
    SpeechBoundaryKind kind = SpeechBoundaryKind::Onset;
    /// Seconds on the capture / stream clock (`vadSynchronization.boundaryRepresentation`).
    double stream_time_seconds = 0.0;
    float rms = 0.0F;
};

struct VoiceActivityDetectorOptions
{
    /// RMS level (linear, 0–1) to enter speech from silence.
    float onset_rms_threshold = 0.012F;
    /// RMS level to leave speech (hysteresis; should be <= onset threshold).
    float release_rms_threshold = 0.008F;
    /// Speech must stay above onset threshold this long before emitting `speech_onset`.
    double min_onset_seconds = 0.02;
    /// Silence must persist this long before emitting `speech_end`.
    double hangover_seconds = 0.10;
};

/// Lightweight energy-based VAD for live prototyping (`inputGainNoiseGateVad` v0).
class VoiceActivityDetector
{
public:
    explicit VoiceActivityDetector(VoiceActivityDetectorOptions options = {});

    void set_options(VoiceActivityDetectorOptions options);
    [[nodiscard]] const VoiceActivityDetectorOptions& options() const noexcept;

    void reset();

    /// Monotonic `stream_time_seconds` strongly recommended.
    void observe_rms(float rms, double stream_time_seconds);

    [[nodiscard]] bool try_pop_boundary(SpeechBoundaryEvent& out);

    [[nodiscard]] static float rms_from_mono(const float* samples, int num_samples) noexcept;

private:
    void push_boundary(SpeechBoundaryKind kind, double stream_time_seconds, float rms);

    VoiceActivityDetectorOptions opt_;
    std::deque<SpeechBoundaryEvent> pending_;

    bool in_speech_ = false;
    bool onset_pending_ = false;
    bool hangover_active_ = false;
    double onset_candidate_since_ = 0.0;
    double silence_since_ = 0.0;
    double last_time_ = 0.0;
    bool have_last_time_ = false;
};

[[nodiscard]] const char* speechBoundaryKindName(SpeechBoundaryKind kind) noexcept;

} // namespace Voice2VocalSynth
