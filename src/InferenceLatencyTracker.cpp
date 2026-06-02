#include "Voice2VocalSynth/InferenceLatencyTracker.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Voice2VocalSynth
{

InferenceLatencyTracker::InferenceLatencyTracker(InferenceLatencyTrackerOptions options)
    : opt_(options)
    , estimate_ms_(options.initial_estimate_ms)
{
}

void InferenceLatencyTracker::set_options(InferenceLatencyTrackerOptions options)
{
    opt_ = options;
    if (!have_estimate_) {
        estimate_ms_ = opt_.initial_estimate_ms;
    }
    while (window_.size() > opt_.window_size) {
        window_.pop_front();
    }
}

const InferenceLatencyTrackerOptions& InferenceLatencyTracker::options() const noexcept
{
    return opt_;
}

void InferenceLatencyTracker::reset()
{
    window_.clear();
    estimate_ms_ = opt_.initial_estimate_ms;
    have_estimate_ = false;
}

bool InferenceLatencyTracker::has_estimate() const noexcept
{
    return have_estimate_;
}

double InferenceLatencyTracker::estimate_ms() const noexcept
{
    return estimate_ms_;
}

double InferenceLatencyTracker::window_median_ms() const
{
    if (window_.empty()) {
        return estimate_ms_;
    }
    std::vector<double> sorted(window_.begin(), window_.end());
    std::sort(sorted.begin(), sorted.end());
    const auto mid = sorted.size() / 2;
    if (sorted.size() % 2 == 1) {
        return sorted[mid];
    }
    return 0.5 * (sorted[mid - 1] + sorted[mid]);
}

void InferenceLatencyTracker::observe_ms(double lag_ms)
{
    if (!std::isfinite(lag_ms) || lag_ms < 0.0) {
        return;
    }

    window_.push_back(lag_ms);
    while (window_.size() > opt_.window_size) {
        window_.pop_front();
    }

    const double target = window_median_ms();
    if (!have_estimate_) {
        estimate_ms_ = target;
        have_estimate_ = true;
        return;
    }

    const double delta = target - estimate_ms_;
    const double step = std::clamp(delta, -opt_.max_update_step_ms, opt_.max_update_step_ms);
    estimate_ms_ = std::max(0.0, estimate_ms_ + step);
}

} // namespace Voice2VocalSynth
