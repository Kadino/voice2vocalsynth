#include <Voice2VocalSynth/RenderPlanner.h>

#include <cassert>
#include <cmath>
#include <iostream>

namespace
{
using namespace Voice2VocalSynth;

bool nearlyEqual(double actual, double expected, double tolerance = 0.001)
{
    return std::abs(actual - expected) <= tolerance;
}

VoicebankAliasIndex makeAliasIndex()
{
    VoicebankAliasIndex index;
    index.addEntry({"ka.wav", "C4_ka_C4", "oto.ini", 1.0, 80.0, -120.0, 35.0, 8.0, 1});
    index.addEntry({"to.wav", "C4_to_C4", "oto.ini", 2.0, 70.0, -100.0, 30.0, 6.0, 2});
    return index;
}

PitchTarget makePitchTarget()
{
    PitchTarget target;
    target.targetFrequencyHz = PitchTargetCalculator::midiToFrequency(60.0);
    target.targetMidi = 60.0;
    target.displayMidiNote = 60;
    target.displayNoteName = "C4";
    return target;
}

void createsRenderEventsFromResolvedMappingPlan()
{
    const VoicebankMappingPlanner mappingPlanner;
    const auto aliasIndex = makeAliasIndex();
    const auto prefixMap = parsePrefixMapContent("C4\tC4_\t_C4\n");
    const auto mappingPlan = mappingPlanner.plan({{"K", "AE", "T"}, "C4"}, aliasIndex, prefixMap);

    RenderPlanRequest request;
    request.mappingPlan = mappingPlan;
    request.pitchTarget = makePitchTarget();
    request.startTimeSeconds = 1.0;
    request.defaultEventDurationMs = 120.0;

    const auto renderPlan = RenderPlanner::plan(request);

    assert(renderPlan.fullyRenderable());
    assert(renderPlan.events.size() == 2);
    assert(renderPlan.events[0].alias == "C4_ka_C4");
    assert(renderPlan.events[0].wavFile == "ka.wav");
    assert(renderPlan.events[0].targetNoteName == "C4");
    assert(renderPlan.events[0].usedPrefixMapCandidate);
    assert(renderPlan.events[0].role == AliasRole::CvSyllable);
    assert(nearlyEqual(renderPlan.events[0].startTimeSeconds, 1.0));
    assert(nearlyEqual(renderPlan.events[0].durationMs, 120.0));
    assert(nearlyEqual(renderPlan.events[0].otoTiming.preutteranceMs, 35.0));

    assert(renderPlan.events[1].alias == "C4_to_C4");
    assert(renderPlan.events[1].usedPartialFallback);
    assert(renderPlan.events[1].renderHint.attenuateVowelTail);
    assert(nearlyEqual(renderPlan.events[1].durationMs, 90.0));
    assert(nearlyEqual(renderPlan.events[1].startTimeSeconds, 1.12));
}

void recordsSkippedEventsForUnresolvedMappings()
{
    VoicebankAliasIndex aliasIndex;
    aliasIndex.addEntry({"ka.wav", "ka", "oto.ini", 0.0, 80.0, -120.0, 35.0, 8.0, 1});

    const VoicebankMappingPlanner mappingPlanner;
    const auto mappingPlan = mappingPlanner.plan({{"K", "AE", "T"}, "C4"}, aliasIndex, {});

    RenderPlanRequest request;
    request.mappingPlan = mappingPlan;
    request.pitchTarget = makePitchTarget();
    const auto renderPlan = RenderPlanner::plan(request);

    assert(!renderPlan.fullyRenderable());
    assert(renderPlan.events.size() == 1);
    assert(renderPlan.skippedEvents.size() == 1);
    assert(renderPlan.skippedEvents[0].sourcePhonemes.size() == 1);
    assert(renderPlan.skippedEvents[0].sourcePhonemes[0] == "T");
    assert(renderPlan.skippedEvents[0].attemptedAliases[0] == "to");
}

} // namespace

int main()
{
    createsRenderEventsFromResolvedMappingPlan();
    recordsSkippedEventsForUnresolvedMappings();

    std::cout << "RenderPlanner tests passed\n";
    return 0;
}
