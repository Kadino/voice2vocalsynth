#include "Voice2VocalSynth/RenderPlanner.h"

#include <algorithm>

namespace Voice2VocalSynth
{
namespace
{

RenderTiming timingFromOto(const OtoEntry& entry)
{
    return {
        entry.offsetMs,
        entry.consonantMs,
        entry.cutoffMs,
        entry.preutteranceMs,
        entry.overlapMs,
    };
}

} // namespace

bool RenderPlan::fullyRenderable() const noexcept
{
    return skippedEvents.empty();
}

RenderPlan RenderPlanner::plan(const RenderPlanRequest& request)
{
    RenderPlan plan;
    auto startTimeSeconds = request.startTimeSeconds;

    for (const auto& mappedEvent : request.mappingPlan.events) {
        const auto durationMs = eventDurationMs(mappedEvent, request.defaultEventDurationMs);

        if (!mappedEvent.resolved() || mappedEvent.resolution.entry == nullptr) {
            SkippedRenderEvent skipped;
            skipped.sourcePhonemes = mappedEvent.originalEvent.sourcePhonemes;
            skipped.attemptedAliases = mappedEvent.resolution.attemptedAliases;
            plan.skippedEvents.push_back(std::move(skipped));
            startTimeSeconds += durationMs / 1000.0;
            continue;
        }

        const auto& entry = *mappedEvent.resolution.entry;
        RenderEvent event;
        event.alias = mappedEvent.resolution.selectedAlias;
        event.wavFile = entry.wavFile;
        event.sourcePhonemes = mappedEvent.originalEvent.sourcePhonemes;
        event.role = mappedEvent.originalEvent.role;
        event.renderHint = mappedEvent.originalEvent.renderHint;
        event.otoTiming = timingFromOto(entry);
        event.startTimeSeconds = startTimeSeconds;
        event.durationMs = durationMs;
        event.targetFrequencyHz = request.pitchTarget.targetFrequencyHz;
        event.targetMidi = request.pitchTarget.targetMidi;
        event.targetNoteName = request.pitchTarget.displayNoteName;
        event.usedPrefixMapCandidate = mappedEvent.usedPrefixMapCandidate();
        event.usedPartialFallback = mappedEvent.originalEvent.isPartialFallback();
        plan.events.push_back(std::move(event));

        startTimeSeconds += durationMs / 1000.0;
    }

    return plan;
}

double RenderPlanner::eventDurationMs(const VoicebankMappedEvent& mappedEvent,
                                      double defaultEventDurationMs)
{
    const auto maxDurationMs = mappedEvent.originalEvent.renderHint.maxDurationMs;
    if (maxDurationMs > 0.0) {
        return std::min(defaultEventDurationMs, maxDurationMs);
    }
    return defaultEventDurationMs;
}

} // namespace Voice2VocalSynth
