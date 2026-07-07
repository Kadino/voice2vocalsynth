#include "Voice2VocalSynth/WhistleDetector.h"

#include <algorithm>
#include <cmath>

namespace Voice2VocalSynth
{
namespace
{

constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] double clamp01(double value) noexcept
{
    return std::max(0.0, std::min(1.0, value));
}

} // namespace

WhistleDetector::WhistleDetector(WhistleDetectorOptions options)
    : opt_(options)
{
}

void WhistleDetector::set_options(WhistleDetectorOptions options)
{
    opt_ = options;
}

const WhistleDetectorOptions& WhistleDetector::options() const noexcept
{
    return opt_;
}

void WhistleDetector::reset()
{
    pending_.clear();
    active_ = false;
    onset_pending_ = false;
    onset_candidate_since_ = 0.0;
    inactive_since_ = 0.0;
}

double WhistleDetector::goertzel_magnitude(const float* samples,
                                           int num_samples,
                                           double sample_rate_hz,
                                           double target_hz) noexcept
{
    if (samples == nullptr || num_samples <= 0 || sample_rate_hz <= 0.0 || target_hz <= 0.0) {
        return 0.0;
    }

    const double omega = 2.0 * kPi * target_hz / sample_rate_hz;
    const double coeff = 2.0 * std::cos(omega);
    double s0 = 0.0;
    double s1 = 0.0;
    double s2 = 0.0;
    for (int i = 0; i < num_samples; ++i) {
        s0 = static_cast<double>(samples[i]) + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - s1 * s2 * coeff));
}


WhistleObservation WhistleDetector::score_at_f0(const float* samples,
                                            int num_samples,
                                            double sample_rate_hz,
                                            double stream_time_seconds,
                                            double f0_hz,
                                            float pitch_confidence) const
{
    WhistleObservation out;
    out.stream_time_seconds = stream_time_seconds;
    out.f0_hz = f0_hz;

    if (f0_hz < opt_.min_whistle_frequency_hz || pitch_confidence < opt_.min_pitch_confidence) {
        return out;
    }

    const double e0 = goertzel_magnitude(samples, num_samples, sample_rate_hz, f0_hz);
    const double e2 = goertzel_magnitude(samples, num_samples, sample_rate_hz, 2.0 * f0_hz);
    const double e3 = goertzel_magnitude(samples, num_samples, sample_rate_hz, 3.0 * f0_hz);
    const double e4 = goertzel_magnitude(samples, num_samples, sample_rate_hz, 4.0 * f0_hz);
    const double harmonic = e2 + e3 + e4;
    const double total = e0 + harmonic;
    out.harmonic_energy_ratio = harmonic / std::max(1.0e-12, total);
    out.narrowband_peak_ratio = e0 / std::max(1.0e-12, total);

    const bool low_harmonics = out.harmonic_energy_ratio <= opt_.max_harmonic_energy_ratio;
    const bool strong_peak = out.narrowband_peak_ratio >= opt_.min_narrowband_peak_ratio;
    if (!low_harmonics || !strong_peak) {
        return out;
    }

    const double harmonicScore =
        clamp01((opt_.max_harmonic_energy_ratio - out.harmonic_energy_ratio) / opt_.max_harmonic_energy_ratio);
    const double peakScore =
        clamp01((out.narrowband_peak_ratio - opt_.min_narrowband_peak_ratio) /
                std::max(1.0e-6, 1.0 - opt_.min_narrowband_peak_ratio));

    out.is_whistle = true;
    out.confidence = static_cast<float>(
        clamp01(0.35 * static_cast<double>(pitch_confidence) + 0.35 * harmonicScore + 0.30 * peakScore));
    return out;
}


WhistleObservation WhistleDetector::analyze(const float* samples,
                                              int num_samples,
                                              double sample_rate_hz,
                                              double stream_time_seconds,
                                              const SimplePitchEstimate& pitch) const
{
    WhistleObservation out;
    out.stream_time_seconds = stream_time_seconds;
    out.f0_hz = pitch.frequencyHz;

    if (samples == nullptr || num_samples < simplePitchEstimatorMinSamples() || sample_rate_hz <= 0.0) {
        return out;
    }

    if (pitch.confidence < opt_.min_pitch_confidence) {
        return out;
    }

    auto best = score_at_f0(samples, num_samples, sample_rate_hz, stream_time_seconds, pitch.frequencyHz,
                            static_cast<float>(pitch.confidence));

    const double octaveUp = pitch.frequencyHz * 2.0;
    if (pitch.frequencyHz < opt_.min_whistle_frequency_hz && octaveUp >= opt_.min_whistle_frequency_hz &&
        octaveUp < sample_rate_hz * 0.45) {
        auto alt = score_at_f0(samples, num_samples, sample_rate_hz, stream_time_seconds, octaveUp,
                               static_cast<float>(pitch.confidence * 0.92));
        if (alt.is_whistle && (!best.is_whistle || alt.confidence > best.confidence)) {
            best = alt;
        }
    }

    return best;
}

void WhistleDetector::push_boundary(bool active, const WhistleObservation& observation)
{
    WhistleBoundaryEvent ev;
    ev.active = active;
    ev.stream_time_seconds = observation.stream_time_seconds;
    ev.confidence = observation.confidence;
    ev.f0_hz = observation.f0_hz;
    pending_.push_back(ev);
}

bool WhistleDetector::try_pop_boundary(WhistleBoundaryEvent& out)
{
    if (pending_.empty()) {
        return false;
    }
    out = pending_.front();
    pending_.pop_front();
    return true;
}

bool WhistleDetector::is_active() const noexcept
{
    return active_;
}

void WhistleDetector::observe(const WhistleObservation& observation)
{
    if (observation.is_whistle) {
        if (!active_) {
            if (!onset_pending_) {
                onset_pending_ = true;
                onset_candidate_since_ = observation.stream_time_seconds;
            } else if (observation.stream_time_seconds - onset_candidate_since_ >= opt_.min_onset_seconds) {
                active_ = true;
                onset_pending_ = false;
                inactive_since_ = observation.stream_time_seconds;
                push_boundary(true, observation);
            }
        } else {
            inactive_since_ = observation.stream_time_seconds;
        }
        return;
    }

    onset_pending_ = false;
    if (!active_) {
        return;
    }

    if (inactive_since_ <= 0.0) {
        inactive_since_ = observation.stream_time_seconds;
    }

    if (observation.stream_time_seconds - inactive_since_ >= opt_.hangover_seconds) {
        active_ = false;
        push_boundary(false, observation);
    }
}

} // namespace Voice2VocalSynth
