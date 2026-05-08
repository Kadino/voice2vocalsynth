#pragma once

#include "Voice2VocalSynth/PhonemeFallbackMapper.h"
#include "Voice2VocalSynth/VoicebankAliasIndex.h"
#include "Voice2VocalSynth/VoicebankScanner.h"

#include <string>
#include <vector>

namespace Voice2VocalSynth
{

struct VoicebankMappingRequest
{
    std::vector<std::string> arpabetPhonemes;
    std::string targetNoteName;
};

struct VoicebankMappedEvent
{
    AliasEvent originalEvent;
    AliasEvent expandedEvent;
    AliasResolution resolution;

    [[nodiscard]] bool resolved() const noexcept;
    [[nodiscard]] bool usedPrefixMapCandidate() const noexcept;
};

struct VoicebankMappingPlan
{
    std::vector<VoicebankMappedEvent> events;
    std::vector<std::string> missingAliases;

    [[nodiscard]] bool fullyResolved() const noexcept;
    [[nodiscard]] std::size_t unresolvedCount() const noexcept;
};

class VoicebankMappingPlanner
{
public:
    VoicebankMappingPlanner();
    explicit VoicebankMappingPlanner(PhonemeFallbackMapper mapper);

    [[nodiscard]] VoicebankMappingPlan plan(const VoicebankMappingRequest& request,
                                            const VoicebankAliasIndex& aliasIndex,
                                            const std::vector<VoicebankPrefixMapEntry>& prefixMapEntries) const;

private:
    PhonemeFallbackMapper mapper_;
};

} // namespace Voice2VocalSynth
