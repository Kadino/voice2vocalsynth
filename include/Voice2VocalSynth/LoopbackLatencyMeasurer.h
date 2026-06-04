#pragma once

#include <cstddef>
#include <vector>

namespace Voice2VocalSynth
{

struct LoopbackLatencyMeasurerOptions
{
    /// MLS-like probe length in samples.
    std::size_t probe_length_samples = 255;
    /// Peak probe level mixed onto the output pass-through during measurement.
    float probe_amplitude = 0.12F;
    /// How long to emit the probe before attempting correlation (samples).
    std::size_t measurement_samples = 24000;
    /// Maximum round-trip delay searched (samples).
    std::size_t max_lag_samples = 24000;
    /// Normalized cross-correlation peak required to accept a result.
    double min_correlation = 0.35;
};

struct LoopbackLatencyMeasurement
{
    /// Round-trip delay from output injection to input detection (ms).
    double round_trip_ms = 0.0;
    /// Peak normalized correlation in [0, 1] (higher is more confident).
    double correlation = 0.0;
    /// Lag in samples corresponding to `round_trip_ms`.
    int lag_samples = 0;
    bool valid = false;
};

/// Injects a deterministic probe on the output and estimates round-trip delay via
/// normalized cross-correlation on the input (`latencyDesign.requirement` measured path).
///
/// Requires a loopback route (physical cable or virtual device) so injected output energy
/// returns on the input while pass-through is active.
class LoopbackLatencyMeasurer
{
public:
    explicit LoopbackLatencyMeasurer(LoopbackLatencyMeasurerOptions options = {});

    void set_options(LoopbackLatencyMeasurerOptions options);
    [[nodiscard]] const LoopbackLatencyMeasurerOptions& options() const noexcept;

    void reset();
    void begin();
    void cancel();

    [[nodiscard]] bool is_measuring() const noexcept;
    [[nodiscard]] bool has_result() const noexcept;
    [[nodiscard]] LoopbackLatencyMeasurement result() const noexcept;

    /// Mixes the probe onto `output_mono` (if non-null) and records `input_mono` (if non-null).
    void process(float* output_mono, const float* input_mono, int num_samples, double sample_rate_hz);

private:
    void finalize_if_ready(double sample_rate_hz);
    [[nodiscard]] LoopbackLatencyMeasurement correlate() const;

    LoopbackLatencyMeasurerOptions opt_;
    std::vector<float> probe_;
    std::vector<float> reference_;
    std::vector<float> capture_;
    std::size_t emitted_samples_ = 0;
    bool measuring_ = false;
    bool have_result_ = false;
    LoopbackLatencyMeasurement result_;
};

} // namespace Voice2VocalSynth
