#pragma once

#include "Voice2VocalSynth/DebugTimeline.h"
#include "Voice2VocalSynth/OfflineRenderer.h"
#include "Voice2VocalSynth/PhonemeFrame.h"
#include "Voice2VocalSynth/PitchTarget.h"
#include "Voice2VocalSynth/RenderPlanner.h"
#include "Voice2VocalSynth/VoicebankMappingPlanner.h"
#include "Voice2VocalSynth/VoicebankScanner.h"

#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Voice2VocalSynth
{

struct StreamingLiveRendererOptions
{
    std::filesystem::path voicebankRoot;
    std::optional<std::filesystem::path> phonemeMappingPath;
    int outputSampleRate = 48000;
    double defaultSourceRecordingFrequencyHz = 261.6255653005986;
    double defaultEventDurationMs = 120.0;
    std::string defaultTargetNoteName = "C4";
};

/// Buffered scheduler that turns committed phoneme frames into mixed synthesis audio.
class StreamingLiveRenderer
{
public:
    explicit StreamingLiveRenderer(StreamingLiveRendererOptions options = {});

    [[nodiscard]] bool configured() const noexcept;
    [[nodiscard]] const std::vector<std::string>& warnings() const noexcept;
    [[nodiscard]] const std::optional<DebugTimeline>& lastTimeline() const noexcept;

    [[nodiscard]] bool configure(const std::filesystem::path& voicebankRoot,
                                 const std::optional<std::filesystem::path>& phonemeMappingPath,
                                 std::string& error);

    void reset();
    void onUtteranceStart();
    void onSustainRelease(double playbackTimeSeconds);

    void onCommittedPhoneme(const PhonemeFrame& frame,
                            const PitchTarget& pitchTarget,
                            double playbackScheduleSeconds,
                            double estimatedLatencyMs);

    void renderBlock(float* output,
                     int numSamples,
                     double playbackStartSeconds,
                     double sampleRateHz);

private:
    struct ScheduledChunk
    {
        double playbackStartSeconds = 0.0;
        std::vector<float> samples;
    };

    void scheduleRenderedEvent(const RenderEvent& event,
                               const VoicebankMappingPlan& mapping,
                               const RenderPlan& renderPlan,
                               const PitchTarget& pitchTarget,
                               double playbackScheduleSeconds,
                               double estimatedLatencyMs);

    StreamingLiveRendererOptions options_;
    bool configured_ = false;
    VoicebankScanResult scan_;
    std::unique_ptr<VoicebankMappingPlanner> planner_;
    std::deque<ScheduledChunk> scheduled_;
    std::vector<std::string> warnings_;
    std::string lastScheduledPhoneme_;
    std::optional<double> sustainReleasePlaybackSeconds_;
    std::optional<DebugTimeline> lastTimeline_;
};

} // namespace Voice2VocalSynth
