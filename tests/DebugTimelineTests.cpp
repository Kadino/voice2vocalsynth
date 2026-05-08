#include <Voice2VocalSynth/DebugTimeline.h>
#include <Voice2VocalSynth/PitchTarget.h>

#include <cassert>
#include <iostream>
#include <string>

namespace
{
using namespace Voice2VocalSynth;

PitchTarget makePitchTarget()
{
    PitchTarget target;
    target.targetFrequencyHz = PitchTargetCalculator::midiToFrequency(60.0);
    target.targetMidi = 60.0;
    target.displayMidiNote = 60;
    target.displayNoteName = "C4";
    target.snapped = true;
    return target;
}

RenderEvent makeRenderEvent()
{
    RenderEvent event;
    event.alias = "C4_ka_C4";
    event.wavFile = "ka.wav";
    event.sourcePhonemes = {"K", "AE"};
    event.role = AliasRole::CvSyllable;
    event.startTimeSeconds = 1.25;
    event.durationMs = 120.0;
    event.targetFrequencyHz = makePitchTarget().targetFrequencyHz;
    event.targetMidi = 60.0;
    event.targetNoteName = "C4";
    event.usedPrefixMapCandidate = true;
    event.renderHint.preserveConsonantDuration = true;
    event.otoTiming.preutteranceMs = 35.0;
    event.otoTiming.overlapMs = 8.0;
    return event;
}

void buildsTimelineFromPlans()
{
    VoicebankMappingPlan mappingPlan;
    mappingPlan.missingAliases = {"to"};

    RenderPlan renderPlan;
    renderPlan.events = {makeRenderEvent()};
    renderPlan.skippedEvents = {{{"T"}, {"to"}}};

    const auto timeline = DebugTimelineExporter::fromPlans(mappingPlan,
                                                           renderPlan,
                                                           makePitchTarget(),
                                                           42.5);

    assert(timeline.renderEvents.size() == 1);
    assert(timeline.skippedEvents.size() == 1);
    assert(timeline.missingAliases.size() == 1);
    assert(timeline.estimatedLatencyMs == 42.5);
    assert(timeline.pitchTarget.displayNoteName == "C4");
}

void exportsReadableJsonTimeline()
{
    DebugTimeline timeline;
    timeline.pitchTarget = makePitchTarget();
    timeline.estimatedLatencyMs = 42.5;
    timeline.renderEvents = {makeRenderEvent()};
    timeline.skippedEvents = {{{"T"}, {"to", "C4_to_C4"}}};
    timeline.missingAliases = {"to"};

    const auto json = DebugTimelineExporter::toJson(timeline);

    assert(json.find("\"schemaVersion\": 1") != std::string::npos);
    assert(json.find("\"displayNoteName\": \"C4\"") != std::string::npos);
    assert(json.find("\"alias\": \"C4_ka_C4\"") != std::string::npos);
    assert(json.find("\"sourcePhonemes\": [\"K\", \"AE\"]") != std::string::npos);
    assert(json.find("\"usedPrefixMapCandidate\": true") != std::string::npos);
    assert(json.find("\"preutteranceMs\": 35.000000") != std::string::npos);
    assert(json.find("\"skippedEvents\"") != std::string::npos);
    assert(json.find("\"missingAliases\": [\"to\"]") != std::string::npos);
}

void escapesJsonStrings()
{
    DebugTimeline timeline;
    timeline.pitchTarget.displayNoteName = "C\"4";
    auto event = makeRenderEvent();
    event.alias = "a\\b";
    timeline.renderEvents = {event};

    const auto json = DebugTimelineExporter::toJson(timeline);

    assert(json.find("C\\\"4") != std::string::npos);
    assert(json.find("a\\\\b") != std::string::npos);
}

} // namespace

int main()
{
    buildsTimelineFromPlans();
    exportsReadableJsonTimeline();
    escapesJsonStrings();

    std::cout << "DebugTimeline tests passed\n";
    return 0;
}
