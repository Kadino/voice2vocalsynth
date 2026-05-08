#include "Voice2VocalSynth/DebugTimeline.h"

#include <iomanip>
#include <sstream>

namespace Voice2VocalSynth
{
namespace
{

std::string escapeJsonString(const std::string& value)
{
    std::ostringstream output;
    for (const auto character : value) {
        switch (character) {
            case '\\':
                output << "\\\\";
                break;
            case '"':
                output << "\\\"";
                break;
            case '\n':
                output << "\\n";
                break;
            case '\r':
                output << "\\r";
                break;
            case '\t':
                output << "\\t";
                break;
            default:
                output << character;
                break;
        }
    }
    return output.str();
}

const char* aliasRoleName(AliasRole role)
{
    switch (role) {
        case AliasRole::Vowel:
            return "vowel";
        case AliasRole::CvSyllable:
            return "cvSyllable";
        case AliasRole::PartialFinalConsonant:
            return "partialFinalConsonant";
        case AliasRole::Unknown:
            return "unknown";
    }
    return "unknown";
}

void writeStringArray(std::ostringstream& json, const std::vector<std::string>& values)
{
    json << "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            json << ", ";
        }
        json << "\"" << escapeJsonString(values[index]) << "\"";
    }
    json << "]";
}

} // namespace

DebugTimeline DebugTimelineExporter::fromPlans(const VoicebankMappingPlan& mappingPlan,
                                               const RenderPlan& renderPlan,
                                               const PitchTarget& pitchTarget,
                                               double estimatedLatencyMs)
{
    DebugTimeline timeline;
    timeline.pitchTarget = pitchTarget;
    timeline.renderEvents = renderPlan.events;
    timeline.skippedEvents = renderPlan.skippedEvents;
    timeline.missingAliases = mappingPlan.missingAliases;
    timeline.estimatedLatencyMs = estimatedLatencyMs;
    return timeline;
}

std::string DebugTimelineExporter::toJson(const DebugTimeline& timeline)
{
    std::ostringstream json;
    json << std::fixed << std::setprecision(6);
    json << "{\n";
    json << "  \"schemaVersion\": 1,\n";
    json << "  \"estimatedLatencyMs\": " << timeline.estimatedLatencyMs << ",\n";
    json << "  \"pitch\": {\n";
    json << "    \"targetFrequencyHz\": " << timeline.pitchTarget.targetFrequencyHz << ",\n";
    json << "    \"targetMidi\": " << timeline.pitchTarget.targetMidi << ",\n";
    json << "    \"displayMidiNote\": " << timeline.pitchTarget.displayMidiNote << ",\n";
    json << "    \"displayNoteName\": \"" << escapeJsonString(timeline.pitchTarget.displayNoteName) << "\",\n";
    json << "    \"usedLowConfidenceFallback\": "
         << (timeline.pitchTarget.usedLowConfidenceFallback ? "true" : "false") << ",\n";
    json << "    \"usedDefaultPitch\": " << (timeline.pitchTarget.usedDefaultPitch ? "true" : "false") << ",\n";
    json << "    \"snapped\": " << (timeline.pitchTarget.snapped ? "true" : "false") << "\n";
    json << "  },\n";
    json << "  \"renderEvents\": [\n";
    for (std::size_t index = 0; index < timeline.renderEvents.size(); ++index) {
        const auto& event = timeline.renderEvents[index];
        json << "    {\n";
        json << "      \"alias\": \"" << escapeJsonString(event.alias) << "\",\n";
        json << "      \"wavFile\": \"" << escapeJsonString(event.wavFile) << "\",\n";
        json << "      \"sourcePhonemes\": ";
        writeStringArray(json, event.sourcePhonemes);
        json << ",\n";
        json << "      \"role\": \"" << aliasRoleName(event.role) << "\",\n";
        json << "      \"startTimeSeconds\": " << event.startTimeSeconds << ",\n";
        json << "      \"durationMs\": " << event.durationMs << ",\n";
        json << "      \"targetFrequencyHz\": " << event.targetFrequencyHz << ",\n";
        json << "      \"targetMidi\": " << event.targetMidi << ",\n";
        json << "      \"targetNoteName\": \"" << escapeJsonString(event.targetNoteName) << "\",\n";
        json << "      \"usedPrefixMapCandidate\": "
             << (event.usedPrefixMapCandidate ? "true" : "false") << ",\n";
        json << "      \"usedPartialFallback\": " << (event.usedPartialFallback ? "true" : "false") << ",\n";
        json << "      \"renderHint\": {\n";
        json << "        \"preserveConsonantDuration\": "
             << (event.renderHint.preserveConsonantDuration ? "true" : "false") << ",\n";
        json << "        \"attenuateVowelTail\": "
             << (event.renderHint.attenuateVowelTail ? "true" : "false") << ",\n";
        json << "        \"vowelTailGain\": " << event.renderHint.vowelTailGain << ",\n";
        json << "        \"maxDurationMs\": " << event.renderHint.maxDurationMs << "\n";
        json << "      },\n";
        json << "      \"otoTiming\": {\n";
        json << "        \"offsetMs\": " << event.otoTiming.offsetMs << ",\n";
        json << "        \"consonantMs\": " << event.otoTiming.consonantMs << ",\n";
        json << "        \"cutoffMs\": " << event.otoTiming.cutoffMs << ",\n";
        json << "        \"preutteranceMs\": " << event.otoTiming.preutteranceMs << ",\n";
        json << "        \"overlapMs\": " << event.otoTiming.overlapMs << "\n";
        json << "      }\n";
        json << "    }" << (index + 1 == timeline.renderEvents.size() ? "\n" : ",\n");
    }
    json << "  ],\n";
    json << "  \"skippedEvents\": [\n";
    for (std::size_t index = 0; index < timeline.skippedEvents.size(); ++index) {
        const auto& event = timeline.skippedEvents[index];
        json << "    {\n";
        json << "      \"sourcePhonemes\": ";
        writeStringArray(json, event.sourcePhonemes);
        json << ",\n";
        json << "      \"attemptedAliases\": ";
        writeStringArray(json, event.attemptedAliases);
        json << "\n";
        json << "    }" << (index + 1 == timeline.skippedEvents.size() ? "\n" : ",\n");
    }
    json << "  ],\n";
    json << "  \"missingAliases\": ";
    writeStringArray(json, timeline.missingAliases);
    json << "\n";
    json << "}\n";
    return json.str();
}

} // namespace Voice2VocalSynth
