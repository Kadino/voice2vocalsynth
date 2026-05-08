#pragma once

#include "Voice2VocalSynth/RenderPlanner.h"

#include <string>
#include <vector>

namespace Voice2VocalSynth
{

struct DebugTimeline
{
    PitchTarget pitchTarget;
    std::vector<RenderEvent> renderEvents;
    std::vector<SkippedRenderEvent> skippedEvents;
    std::vector<std::string> missingAliases;
    double estimatedLatencyMs = 0.0;
};

class DebugTimelineExporter
{
public:
    [[nodiscard]] static DebugTimeline fromPlans(const VoicebankMappingPlan& mappingPlan,
                                                 const RenderPlan& renderPlan,
                                                 const PitchTarget& pitchTarget,
                                                 double estimatedLatencyMs = 0.0);
    [[nodiscard]] static std::string toJson(const DebugTimeline& timeline);
};

} // namespace Voice2VocalSynth
