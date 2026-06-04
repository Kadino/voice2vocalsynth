#include "Voice2VocalSynth/LoopbackLatencyMeasurer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Voice2VocalSynth
{
namespace
{

std::vector<float> make_probe_pattern(std::size_t length)
{
    std::vector<float> probe(length, 0.0F);
    std::uint16_t lfsr = 0xACE1U;
    for (std::size_t i = 0; i < length; ++i) {
        const int bit = static_cast<int>(lfsr & 1U);
        lfsr = static_cast<std::uint16_t>(lfsr >> 1U);
        if (bit != 0) {
            lfsr = static_cast<std::uint16_t>(lfsr ^ 0xB400U);
        }
        probe[i] = bit != 0 ? 1.0F : -1.0F;
    }
    return probe;
}

} // namespace

LoopbackLatencyMeasurer::LoopbackLatencyMeasurer(LoopbackLatencyMeasurerOptions options)
    : opt_(options)
    , probe_(make_probe_pattern(options.probe_length_samples))
{
}

void LoopbackLatencyMeasurer::set_options(LoopbackLatencyMeasurerOptions options)
{
    opt_ = options;
    if (probe_.size() != opt_.probe_length_samples) {
        probe_ = make_probe_pattern(opt_.probe_length_samples);
    }
}

const LoopbackLatencyMeasurerOptions& LoopbackLatencyMeasurer::options() const noexcept
{
    return opt_;
}

void LoopbackLatencyMeasurer::reset()
{
    measuring_ = false;
    have_result_ = false;
    emitted_samples_ = 0;
    reference_.clear();
    capture_.clear();
    result_ = {};
}

void LoopbackLatencyMeasurer::begin()
{
    reset();
    measuring_ = true;
    reference_.reserve(opt_.measurement_samples);
    capture_.reserve(opt_.measurement_samples + opt_.max_lag_samples);
}

void LoopbackLatencyMeasurer::cancel()
{
    measuring_ = false;
    reference_.clear();
    capture_.clear();
    emitted_samples_ = 0;
}

bool LoopbackLatencyMeasurer::is_measuring() const noexcept
{
    return measuring_;
}

bool LoopbackLatencyMeasurer::has_result() const noexcept
{
    return have_result_;
}

LoopbackLatencyMeasurement LoopbackLatencyMeasurer::result() const noexcept
{
    return result_;
}

void LoopbackLatencyMeasurer::process(float* output_mono,
                                      const float* input_mono,
                                      int num_samples,
                                      double sample_rate_hz)
{
    if (!measuring_ || num_samples <= 0 || sample_rate_hz <= 0.0) {
        return;
    }

    for (int i = 0; i < num_samples; ++i) {
        if (emitted_samples_ < opt_.measurement_samples) {
            const float probeSample =
                opt_.probe_amplitude * probe_[emitted_samples_ % probe_.size()];
            if (output_mono != nullptr) {
                output_mono[i] += probeSample;
            }
            reference_.push_back(probeSample);
            ++emitted_samples_;
        }

        if (input_mono != nullptr) {
            capture_.push_back(input_mono[i]);
        } else {
            capture_.push_back(0.0F);
        }
    }

    finalize_if_ready(sample_rate_hz);
}

void LoopbackLatencyMeasurer::finalize_if_ready(double sample_rate_hz)
{
    if (!measuring_) {
        return;
    }
    if (emitted_samples_ < opt_.measurement_samples) {
        return;
    }
    if (capture_.size() < reference_.size() + opt_.max_lag_samples) {
        return;
    }

    result_ = correlate();
    if (result_.valid) {
        result_.round_trip_ms =
            (static_cast<double>(result_.lag_samples) * 1000.0) / sample_rate_hz;
    }
    measuring_ = false;
    have_result_ = true;
}

LoopbackLatencyMeasurement LoopbackLatencyMeasurer::correlate() const
{
    LoopbackLatencyMeasurement out;
    if (reference_.empty() || capture_.size() < reference_.size()) {
        return out;
    }

    const std::size_t maxLag =
        std::min(opt_.max_lag_samples, capture_.size() - reference_.size());

    double bestScore = -1.0;
    std::size_t bestLag = 0;

    for (std::size_t lag = 0; lag <= maxLag; ++lag) {
        double dot = 0.0;
        double refEnergy = 0.0;
        double capEnergy = 0.0;
        for (std::size_t i = 0; i < reference_.size(); ++i) {
            const double ref = static_cast<double>(reference_[i]);
            const double cap = static_cast<double>(capture_[lag + i]);
            dot += ref * cap;
            refEnergy += ref * ref;
            capEnergy += cap * cap;
        }
        if (refEnergy <= 1.0e-12 || capEnergy <= 1.0e-12) {
            continue;
        }
        const double score = dot / std::sqrt(refEnergy * capEnergy);
        if (score > bestScore) {
            bestScore = score;
            bestLag = lag;
        }
    }

    if (bestScore < opt_.min_correlation) {
        return out;
    }

    out.lag_samples = static_cast<int>(bestLag);
    out.correlation = bestScore;
    out.valid = true;
    return out;
}

} // namespace Voice2VocalSynth
