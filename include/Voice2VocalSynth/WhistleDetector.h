#pragma once

#include "Voice2VocalSynth/SimplePitchEstimator.h"

#include <deque>

namespace Voice2VocalSynth
{

struct WhistleDetectorOptions
{
    /// Whistles are usually well above typical speech fundamentals.
    double min_whistle_frequency_hz = 400.0;
    double min_pitch_confidence = 0.45;
    /// Upper bound on harmonic energy relative to the narrowband peak (HNR-style proxy).
    double max_harmonic_energy_ratio = 0.38;
    /// Minimum narrowband peak energy relative to total window RMS.
    double min_narrowband_peak_ratio = 0.55;
    /// Require whistle evidence for this long before asserting onset.
    double min_onset_seconds = 0.04;
    /// Hold whistle state through brief dips.
    double hangover_seconds = 0.08;
};

struct WhistleObservation
{
    double stream_time_seconds = 0.0;
    bool is_whistle = false;
    float confidence = 0.0F;
    double f0_hz = 0.0;
    double harmonic_energy_ratio = 0.0;
    double narrowband_peak_ratio = 0.0;
};

struct WhistleBoundaryEvent
{
    bool active = false;
    double stream_time_seconds = 0.0;
    float confidence = 0.0F;
    double f0_hz = 0.0;
};

/// Lightweight narrowband / low-harmonic detector (`whistleDetection` v0), separate from phoneme ONNX.
class WhistleDetector
{
public:
    explicit WhistleDetector(WhistleDetectorOptions options = {});

    void set_options(WhistleDetectorOptions options);
    [[nodiscard]] const WhistleDetectorOptions& options() const noexcept;

    void reset();

    /// Uses the supplied pitch estimate (e.g. from `estimatePitchFromMono`) on the same window.
    WhistleObservation analyze(const float* samples,
                               int num_samples,
                               double sample_rate_hz,
                               double stream_time_seconds,
                               const SimplePitchEstimate& pitch) const;

    /// Stateful wrapper that emits onset/end edges for logging and gating.
    void observe(const WhistleObservation& observation);

    [[nodiscard]] bool is_active() const noexcept;
    [[nodiscard]] bool try_pop_boundary(WhistleBoundaryEvent& out);

    [[nodiscard]] static double goertzel_magnitude(const float* samples,
                                                   int num_samples,
                                                   double sample_rate_hz,
                                                   double target_hz) noexcept;

private:
    [[nodiscard]] WhistleObservation score_at_f0(const float* samples,
                                                 int num_samples,
                                                 double sample_rate_hz,
                                                 double stream_time_seconds,
                                                 double f0_hz,
                                                 float pitch_confidence) const;

    void push_boundary(bool active, const WhistleObservation& observation);

    WhistleDetectorOptions opt_;
    std::deque<WhistleBoundaryEvent> pending_;
    bool active_ = false;
    bool onset_pending_ = false;
    double onset_candidate_since_ = 0.0;
    double inactive_since_ = 0.0;
};

} // namespace Voice2VocalSynth
