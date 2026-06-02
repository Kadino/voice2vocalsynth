#pragma once

#include "Voice2VocalSynth/LatencyBudget.h"

namespace Voice2VocalSynth
{

/// Maps analysis-time stream boundaries to perceived playback time (`vadSynchronization` v1).
class PlaybackBoundaryMapper
{
public:
    [[nodiscard]] static double analysisToPlaybackSeconds(double analysis_time_seconds,
                                                          const LatencyBreakdown& breakdown,
                                                          double inference_jitter_ms) noexcept;

    [[nodiscard]] static double stableLatencyMs(const LatencyBreakdown& breakdown) noexcept;
};

} // namespace Voice2VocalSynth
