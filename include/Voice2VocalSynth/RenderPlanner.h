#pragma once

#include "Voice2VocalSynth/PitchTarget.h"
#include "Voice2VocalSynth/VoicebankMappingPlanner.h"

#include <string>
#include <vector>

namespace Voice2VocalSynth
{

struct RenderPlanRequest
{
    VoicebankMappingPlan mappingPlan;
    PitchTarget pitchTarget;
    double startTimeSeconds = 0.0;
    double defaultEventDurationMs = 120.0;
};

struct RenderTiming
{
    double offsetMs = 0.0;
    double consonantMs = 0.0;
    double cutoffMs = 0.0;
    double preutteranceMs = 0.0;
    double overlapMs = 0.0;
};

struct RenderEvent
{
    std::string alias;
    std::string wavFile;
    std::vector<std::string> sourcePhonemes;
    AliasRole role = AliasRole::Unknown;
    RenderHint renderHint;
    RenderTiming otoTiming;
    double startTimeSeconds = 0.0;
    double durationMs = 0.0;
    double targetFrequencyHz = 0.0;
    double targetMidi = 0.0;
    std::string targetNoteName;
    /// Fundamental frequency (Hz) at which this WAV was recorded; <= 0 means use
    /// `OfflineRenderOptions::defaultSourceRecordingFrequencyHz` in the offline renderer.
    double sourceRecordingFrequencyHz = 0.0;
    bool usedPrefixMapCandidate = false;
    bool usedPartialFallback = false;
    /// Playback timeline seconds when sustain should end (0 = use full event duration).
    double perceivedUtteranceEndSeconds = 0.0;
};

struct SkippedRenderEvent
{
    std::vector<std::string> sourcePhonemes;
    std::vector<std::string> attemptedAliases;
};

struct RenderPlan
{
    std::vector<RenderEvent> events;
    std::vector<SkippedRenderEvent> skippedEvents;

    [[nodiscard]] bool fullyRenderable() const noexcept;
};

class RenderPlanner
{
public:
    [[nodiscard]] static RenderPlan plan(const RenderPlanRequest& request);

private:
    [[nodiscard]] static double eventDurationMs(const VoicebankMappedEvent& mappedEvent,
                                                double defaultEventDurationMs);
};

} // namespace Voice2VocalSynth
