#include "Voice2VocalSynth/PlaybackBoundaryMapper.h"

#include <algorithm>
#include <cmath>

namespace Voice2VocalSynth
{

double PlaybackBoundaryMapper::stableLatencyMs(const LatencyBreakdown& breakdown) noexcept
{
    return breakdown.endToEndMonitoringLatencyMs();
}

double PlaybackBoundaryMapper::analysisToPlaybackSeconds(double analysis_time_seconds,
                                                           const LatencyBreakdown& breakdown,
                                                           double inference_jitter_ms) noexcept
{
    if (!std::isfinite(analysis_time_seconds)) {
        return 0.0;
    }

    const double jitter = std::max(0.0, inference_jitter_ms);
    const double totalMs = stableLatencyMs(breakdown) + jitter;
    return analysis_time_seconds + totalMs / 1000.0;
}

} // namespace Voice2VocalSynth
