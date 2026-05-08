#include "Voice2VocalSynth/VoicebankMappingPlanner.h"

#include <algorithm>
#include <utility>

namespace Voice2VocalSynth
{
namespace
{

void appendMissingAliases(VoicebankMappingPlan& plan, const AliasResolution& resolution)
{
    for (const auto& alias : resolution.missingCandidates) {
        if (std::find(plan.missingAliases.begin(), plan.missingAliases.end(), alias) ==
            plan.missingAliases.end()) {
            plan.missingAliases.push_back(alias);
        }
    }
}

} // namespace

bool VoicebankMappedEvent::resolved() const noexcept
{
    return resolution.resolved;
}

bool VoicebankMappedEvent::usedPrefixMapCandidate() const noexcept
{
    return resolution.resolved && resolution.reason == "prefixMap";
}

bool VoicebankMappingPlan::fullyResolved() const noexcept
{
    return unresolvedCount() == 0;
}

std::size_t VoicebankMappingPlan::unresolvedCount() const noexcept
{
    return static_cast<std::size_t>(
        std::count_if(events.begin(), events.end(), [](const auto& event) {
            return !event.resolved();
        }));
}

VoicebankMappingPlanner::VoicebankMappingPlanner()
    : mapper_()
{
}

VoicebankMappingPlanner::VoicebankMappingPlanner(PhonemeFallbackMapper mapper)
    : mapper_(std::move(mapper))
{
}

VoicebankMappingPlan VoicebankMappingPlanner::plan(
    const VoicebankMappingRequest& request,
    const VoicebankAliasIndex& aliasIndex,
    const std::vector<VoicebankPrefixMapEntry>& prefixMapEntries) const
{
    VoicebankMappingPlan plan;
    const auto mappedEvents = mapper_.mapPhonemes(request.arpabetPhonemes);
    plan.events.reserve(mappedEvents.size());

    for (const auto& event : mappedEvents) {
        auto expanded = applyPrefixMapToAliasEvent(event, prefixMapEntries, request.targetNoteName);
        auto resolution = aliasIndex.resolve(expanded);

        VoicebankMappedEvent mappedEvent;
        mappedEvent.originalEvent = event;
        mappedEvent.expandedEvent = std::move(expanded);
        mappedEvent.resolution = std::move(resolution);
        appendMissingAliases(plan, mappedEvent.resolution);
        plan.events.push_back(std::move(mappedEvent));
    }

    return plan;
}

} // namespace Voice2VocalSynth
