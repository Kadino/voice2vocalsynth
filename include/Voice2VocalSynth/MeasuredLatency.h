#pragma once

#include <algorithm>

#include "Voice2VocalSynth/LatencyBudget.h"
#include "Voice2VocalSynth/LoopbackLatencyMeasurer.h"

namespace Voice2VocalSynth
{

/// Estimated budget plus an optional loopback measurement (`latencyDesign.requirement`).
struct MeasuredLatencySummary
{
    LatencyBreakdown estimated {};
    LoopbackLatencyMeasurement loopback {};
    double inference_jitter_ms = 0.0;

    [[nodiscard]] bool has_loopback() const noexcept { return loopback.valid; }

    [[nodiscard]] double estimated_end_to_end_ms() const noexcept
    {
        return estimated.endToEndMonitoringLatencyMs();
    }

    [[nodiscard]] double effective_end_to_end_ms() const noexcept
    {
        if (loopback.valid) {
            return loopback.round_trip_ms;
        }
        return estimated.endToEndMonitoringLatencyMs();
    }

    [[nodiscard]] double loopback_residual_ms() const noexcept
    {
        if (!loopback.valid) {
            return 0.0;
        }
        return loopback.round_trip_ms - estimated.endToEndMonitoringLatencyMs();
    }

    [[nodiscard]] double playback_mapping_ms() const noexcept
    {
        return effective_end_to_end_ms() + std::max(0.0, inference_jitter_ms);
    }
};

} // namespace Voice2VocalSynth
