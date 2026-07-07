#include "Voice2VocalSynth/StreamingLiveRenderer.h"

#include "Voice2VocalSynth/PhonemeMappingConfigLoader.h"
#include "Voice2VocalSynth/PitchTarget.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace Voice2VocalSynth
{

StreamingLiveRenderer::StreamingLiveRenderer(StreamingLiveRendererOptions options)
    : options_(std::move(options))
{
}

bool StreamingLiveRenderer::configured() const noexcept
{
    return configured_;
}

const std::vector<std::string>& StreamingLiveRenderer::warnings() const noexcept
{
    return warnings_;
}

const std::optional<DebugTimeline>& StreamingLiveRenderer::lastTimeline() const noexcept
{
    return lastTimeline_;
}

bool StreamingLiveRenderer::configure(const std::filesystem::path& voicebankRoot,
                                        const std::optional<std::filesystem::path>& phonemeMappingPath,
                                        std::string& error)
{
    error.clear();
    warnings_.clear();
    reset();

    options_.voicebankRoot = voicebankRoot;
    options_.phonemeMappingPath = phonemeMappingPath;
    scan_ = VoicebankScanner::scan(voicebankRoot);
    if (!scan_.foundOtoIni()) {
        error = "No oto.ini found under voicebank root: " + voicebankRoot.string();
        configured_ = false;
        return false;
    }

    const auto mappingLoad = PhonemeMappingConfigLoader::loadEffective(options_.phonemeMappingPath);
    planner_ = std::make_unique<VoicebankMappingPlanner>(PhonemeFallbackMapper(mappingLoad.options));
    if (scan_.hasBankRootRecordingPitch()) {
        options_.defaultSourceRecordingFrequencyHz = scan_.bankRootRecordingFrequencyHz;
    }

    configured_ = true;
    return true;
}

void StreamingLiveRenderer::reset()
{
    scheduled_.clear();
    lastScheduledPhoneme_.clear();
    sustainReleasePlaybackSeconds_.reset();
    lastTimeline_.reset();
}

void StreamingLiveRenderer::onUtteranceStart()
{
    lastScheduledPhoneme_.clear();
    sustainReleasePlaybackSeconds_.reset();
}

void StreamingLiveRenderer::onSustainRelease(const double playbackTimeSeconds)
{
    sustainReleasePlaybackSeconds_ = playbackTimeSeconds;
}

void StreamingLiveRenderer::scheduleRenderedEvent(const RenderEvent& event,
                                                    const VoicebankMappingPlan& mapping,
                                                    const RenderPlan& renderPlan,
                                                    const PitchTarget& pitchTarget,
                                                    const double playbackScheduleSeconds,
                                                    const double estimatedLatencyMs)
{
    RenderPlan plan;
    plan.events.push_back(event);
    OfflineRenderOptions renderOptions;
    renderOptions.voicebankRoot = options_.voicebankRoot;
    renderOptions.outputSampleRate = options_.outputSampleRate;
    renderOptions.defaultSourceRecordingFrequencyHz = options_.defaultSourceRecordingFrequencyHz;

    const auto rendered = OfflineRenderer::render(plan, renderOptions);
    for (const auto& warning : rendered.warnings) {
        warnings_.push_back(warning);
    }
    if (!rendered.ok || rendered.mono.empty()) {
        return;
    }

    ScheduledChunk chunk;
    chunk.playbackStartSeconds = playbackScheduleSeconds;
    chunk.samples = std::move(rendered.mono);
    scheduled_.push_back(std::move(chunk));
    lastTimeline_ = DebugTimelineExporter::fromPlans(mapping, renderPlan, pitchTarget, estimatedLatencyMs);
}

void StreamingLiveRenderer::onCommittedPhoneme(const PhonemeFrame& frame,
                                                 const PitchTarget& pitchTarget,
                                                 const double playbackScheduleSeconds,
                                                 const double estimatedLatencyMs)
{
    if (!configured_ || planner_ == nullptr || frame.arpabet.empty()) {
        return;
    }
    if (frame.arpabet == lastScheduledPhoneme_) {
        return;
    }

    VoicebankMappingRequest mapRequest;
    mapRequest.arpabetPhonemes = {frame.arpabet};
    mapRequest.targetNoteName = pitchTarget.displayNoteName.empty() ? options_.defaultTargetNoteName
                                                                    : pitchTarget.displayNoteName;
    const auto mapping = planner_->plan(mapRequest, scan_.aliasIndex, scan_.prefixMapEntries);
    if (mapping.events.empty() || !mapping.events.front().resolved()) {
        std::ostringstream warning;
        warning << "live render skipped phoneme=" << frame.arpabet;
        warnings_.push_back(warning.str());
        return;
    }

    RenderPlanRequest planRequest;
    planRequest.mappingPlan = mapping;
    planRequest.pitchTarget = pitchTarget;
    planRequest.startTimeSeconds = 0.0;
    planRequest.defaultEventDurationMs = options_.defaultEventDurationMs;
    const auto renderPlan = RenderPlanner::plan(planRequest);
    if (renderPlan.events.empty()) {
        return;
    }

    auto event = renderPlan.events.front();
    if (sustainReleasePlaybackSeconds_) {
        event.perceivedUtteranceEndSeconds = *sustainReleasePlaybackSeconds_;
    } else {
        event.perceivedUtteranceEndSeconds = playbackScheduleSeconds + (event.durationMs / 1000.0);
    }
    scheduleRenderedEvent(event, mapping, renderPlan, pitchTarget, playbackScheduleSeconds, estimatedLatencyMs);
    lastScheduledPhoneme_ = frame.arpabet;
}

void StreamingLiveRenderer::renderBlock(float* output,
                                          const int numSamples,
                                          const double playbackStartSeconds,
                                          const double sampleRateHz)
{
    if (output == nullptr || numSamples <= 0 || sampleRateHz <= 0.0) {
        return;
    }

    std::fill(output, output + numSamples, 0.0F);
    const double blockDuration = static_cast<double>(numSamples) / sampleRateHz;
    const double blockEnd = playbackStartSeconds + blockDuration;

    while (!scheduled_.empty()) {
        const auto& front = scheduled_.front();
        const double chunkEnd = front.playbackStartSeconds +
                                (static_cast<double>(front.samples.size()) / sampleRateHz);
        if (chunkEnd < playbackStartSeconds) {
            scheduled_.pop_front();
            continue;
        }
        break;
    }

    for (const auto& chunk : scheduled_) {
        const double chunkStart = chunk.playbackStartSeconds;
        double chunkEnd = chunkStart + (static_cast<double>(chunk.samples.size()) / sampleRateHz);
        if (sustainReleasePlaybackSeconds_) {
            chunkEnd = std::min(chunkEnd, *sustainReleasePlaybackSeconds_);
        }
        if (chunkEnd <= playbackStartSeconds || chunkStart >= blockEnd) {
            continue;
        }

        const double overlapStart = std::max(playbackStartSeconds, chunkStart);
        const double overlapEnd = std::min(blockEnd, chunkEnd);
        const auto startIndex =
            static_cast<std::size_t>(std::floor((overlapStart - chunkStart) * sampleRateHz));
        const auto outStartIndex =
            static_cast<std::size_t>(std::floor((overlapStart - playbackStartSeconds) * sampleRateHz));
        const auto sampleCount = static_cast<std::size_t>(
            std::ceil((overlapEnd - overlapStart) * sampleRateHz));
        for (std::size_t index = 0; index < sampleCount; ++index) {
            const auto chunkIndex = startIndex + index;
            const auto outIndex = outStartIndex + index;
            if (chunkIndex >= chunk.samples.size() || outIndex >= static_cast<std::size_t>(numSamples)) {
                break;
            }
            output[outIndex] += chunk.samples[chunkIndex];
        }
    }
}

} // namespace Voice2VocalSynth
