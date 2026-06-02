#pragma once

#include <deque>

namespace Voice2VocalSynth
{

struct InferenceLatencyTrackerOptions
{
    /// Number of recent queue+inference lag samples in the moving window.
    std::size_t window_size = 16;
    /// Maximum change applied to the estimate per observation (ms).
    double max_update_step_ms = 8.0;
    /// Initial estimate before any samples (ms).
    double initial_estimate_ms = 0.0;
};

/// Bounded moving estimate of ONNX model-plus-queue lag (`vadSynchronization.latencyAlignment.inferenceJitter`).
class InferenceLatencyTracker
{
public:
    explicit InferenceLatencyTracker(InferenceLatencyTrackerOptions options = {});

    void set_options(InferenceLatencyTrackerOptions options);
    [[nodiscard]] const InferenceLatencyTrackerOptions& options() const noexcept;

    void reset();

    /// Observes a single lag sample in milliseconds (enqueue → completion).
    void observe_ms(double lag_ms);

    [[nodiscard]] double estimate_ms() const noexcept;
    [[nodiscard]] bool has_estimate() const noexcept;

private:
    [[nodiscard]] double window_median_ms() const;

    InferenceLatencyTrackerOptions opt_;
    std::deque<double> window_;
    double estimate_ms_ = 0.0;
    bool have_estimate_ = false;
};

} // namespace Voice2VocalSynth
