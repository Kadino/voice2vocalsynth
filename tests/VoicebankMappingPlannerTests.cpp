#include <Voice2VocalSynth/VoicebankMappingPlanner.h>

#include <cassert>
#include <iostream>

namespace
{
using namespace Voice2VocalSynth;

VoicebankAliasIndex makeAliasIndex(std::initializer_list<std::string> aliases)
{
    VoicebankAliasIndex index;
    for (const auto& alias : aliases) {
        index.addAlias(alias);
    }
    return index;
}

void resolvesMappedEventsWithPrefixMapFirst()
{
    const VoicebankMappingPlanner planner;
    const auto aliasIndex = makeAliasIndex({"C4_ka_C4", "C4_to_C4", "ka", "to"});
    const auto prefixMap = parsePrefixMapContent("C4\tC4_\t_C4\n");

    const auto plan = planner.plan({{"K", "AE", "T"}, "C4"}, aliasIndex, prefixMap);

    assert(plan.fullyResolved());
    assert(plan.events.size() == 2);
    assert(plan.events[0].resolution.selectedAlias == "C4_ka_C4");
    assert(plan.events[0].usedPrefixMapCandidate());
    assert(plan.events[1].resolution.selectedAlias == "C4_to_C4");
    assert(plan.events[1].usedPrefixMapCandidate());
    assert(plan.events[1].originalEvent.isPartialFallback());
}

void fallsBackToOriginalAliasWhenPrefixedAliasIsMissing()
{
    const VoicebankMappingPlanner planner;
    const auto aliasIndex = makeAliasIndex({"ka", "to"});
    const auto prefixMap = parsePrefixMapContent("C4\tC4_\t_C4\n");

    const auto plan = planner.plan({{"K", "AE", "T"}, "C4"}, aliasIndex, prefixMap);

    assert(plan.fullyResolved());
    assert(plan.events.size() == 2);
    assert(plan.events[0].resolution.selectedAlias == "ka");
    assert(!plan.events[0].usedPrefixMapCandidate());
    assert(plan.events[0].resolution.attemptedAliases[0] == "C4_ka_C4");
    assert(plan.events[0].resolution.attemptedAliases[1] == "ka");
    assert(!plan.missingAliases.empty());
    assert(plan.missingAliases[0] == "C4_ka_C4");
}

void reportsUnresolvedAliasDiagnostics()
{
    const VoicebankMappingPlanner planner;
    const auto aliasIndex = makeAliasIndex({"ka"});

    const auto plan = planner.plan({{"K", "AE", "T"}, "C4"}, aliasIndex, {});

    assert(!plan.fullyResolved());
    assert(plan.unresolvedCount() == 1);
    assert(plan.events.size() == 2);
    assert(plan.events[0].resolution.selectedAlias == "ka");
    assert(!plan.events[1].resolution.resolved);
    assert(plan.events[1].resolution.missingCandidates[0] == "to");
    assert(plan.missingAliases.size() == 1);
    assert(plan.missingAliases[0] == "to");
}

void supportsCustomFallbackMapperOptions()
{
    auto options = PhonemeFallbackMapper::makeDefaultOptions();
    options.consonantSubstitutions["TH"] = "t";
    options.consonantFallbackCandidates["TH"] = {"s"};
    const VoicebankMappingPlanner planner(PhonemeFallbackMapper(std::move(options)));
    const auto aliasIndex = makeAliasIndex({"sa"});

    const auto plan = planner.plan({{"TH", "AE"}, ""}, aliasIndex, {});

    assert(plan.fullyResolved());
    assert(plan.events.size() == 1);
    assert(plan.events[0].resolution.selectedAlias == "sa");
    assert(plan.events[0].resolution.candidateIndex == 1);
}

} // namespace

int main()
{
    resolvesMappedEventsWithPrefixMapFirst();
    fallsBackToOriginalAliasWhenPrefixedAliasIsMissing();
    reportsUnresolvedAliasDiagnostics();
    supportsCustomFallbackMapperOptions();

    std::cout << "VoicebankMappingPlanner tests passed\n";
    return 0;
}
