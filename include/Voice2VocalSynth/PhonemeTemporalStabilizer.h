#pragma once

#include "Voice2VocalSynth/PhonemeFrame.h"

#include <deque>
#include <string>

namespace Voice2VocalSynth
{

struct PhonemeTemporalStabilizerOptions
{
    /// Minimum duration for a committed segment (shorter blips are dropped).
    double min_segment_seconds = 0.05;
    /// A competing label must be observed this long (at sufficient confidence) before switching.
    double candidate_stable_seconds = 0.08;
    /// Confidence floor for treating a frame as voiced phoneme evidence.
    float min_observation_confidence = 0.35F;
    /// Extra confidence required to abandon the current label for a new one.
    float hysteresis_confidence_delta = 0.08F;
    /// Unvoiced / low-confidence frames must persist this long before closing the current segment.
    double silence_finalize_seconds = 0.12;
};

struct PhonemeTemporalObservation
{
    double stream_time_seconds = 0.0;
    /// Empty or whitespace-only is treated as silence / no phoneme.
    std::string arpabet;
    float confidence = 0.0F;
};

/// Buffers raw streaming hypotheses and emits stabilized `PhonemeFrame` segments with
/// onset/end times on the capture stream clock (`phonemeDetection.temporalStabilizer` v1).
class PhonemeTemporalStabilizer
{
public:
    explicit PhonemeTemporalStabilizer(PhonemeTemporalStabilizerOptions options = {});

    void set_options(PhonemeTemporalStabilizerOptions options);
    [[nodiscard]] const PhonemeTemporalStabilizerOptions& options() const noexcept;

    void reset();

    /// Monotonic `stream_time_seconds` strongly recommended (same clock as live capture).
    void observe(const PhonemeTemporalObservation& observation);

    [[nodiscard]] bool try_pop_committed(PhonemeFrame& out);

private:
    void flush_committed();

    [[nodiscard]] static PhonemeFrame make_frame(const std::string& normalized_arpabet,
                                                 float peak_confidence,
                                                 double onset_seconds,
                                                 double end_seconds);

    PhonemeTemporalStabilizerOptions opt_;
    std::deque<PhonemeFrame> committed_;

    std::string active_label_;
    double active_onset_ = 0.0;
    double active_same_label_last_time_ = 0.0;
    float active_peak_confidence_ = 0.0F;

    std::string candidate_label_;
    double candidate_since_ = 0.0;
    float candidate_peak_confidence_ = 0.0F;

    bool in_silence_run_ = false;
    double silence_since_ = 0.0;

    double last_observation_time_ = 0.0;
    bool have_last_observation_time_ = false;
};

} // namespace Voice2VocalSynth
