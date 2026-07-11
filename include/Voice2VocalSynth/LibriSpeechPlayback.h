#pragma once

#include "Voice2VocalSynth/LibriSpeechDataset.h"
#include "Voice2VocalSynth/LinuxVirtualAudio.h"

#include <filesystem>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Voice2VocalSynth
{

inline constexpr double kDefaultLibriSpeechPlaybackGapSeconds = 0.5;

struct LibriSpeechPlaybackClip
{
    std::string utteranceId;
    std::filesystem::path flacPath;
    double durationSeconds = 0.0;
    double startOffsetSeconds = 0.0;
};

struct LibriSpeechPlaybackPlan
{
    std::vector<LibriSpeechPlaybackClip> clips;
    double gapSeconds = kDefaultLibriSpeechPlaybackGapSeconds;
    double totalDurationSeconds = 0.0;
    std::string playbackDevice;
    std::string routeId;
    std::filesystem::path runDirectory;
    std::int64_t playbackStartedSteadyNs = 0;
};

struct LibriSpeechPlaybackDurationEntry
{
    std::string utteranceId;
    double durationSeconds = 0.0;
};

struct LibriSpeechPlaybackBuildResult
{
    bool ok = false;
    LibriSpeechPlaybackPlan plan;
    std::string error;
};

struct LibriSpeechPlaybackManifestLoadResult
{
    bool ok = false;
    LibriSpeechPlaybackPlan plan;
    std::string error;
};

[[nodiscard]] std::filesystem::path defaultLibriSpeechPlaybackManifestPath(
    const std::filesystem::path& runDirectory);

/// Parses `utterance-id<TAB>duration-seconds` lines from ffprobe output collection.
[[nodiscard]] std::vector<LibriSpeechPlaybackDurationEntry> parseLibriSpeechDurationTsv(
    std::string_view text,
    std::string& error);

/// Builds a real-time playback plan with scheduled start offsets and total duration.
[[nodiscard]] LibriSpeechPlaybackBuildResult buildLibriSpeechPlaybackPlan(
    const std::vector<LibriSpeechUtterance>& utterances,
    const std::vector<LibriSpeechPlaybackDurationEntry>& durations,
    const LinuxVirtualAudioRoute& route,
    double gapSeconds,
    const std::filesystem::path& runDirectory);

[[nodiscard]] bool writeLibriSpeechPlaybackManifest(const LibriSpeechPlaybackPlan& plan,
                                                    const std::filesystem::path& manifestPath,
                                                    std::string& error);

[[nodiscard]] LibriSpeechPlaybackManifestLoadResult parseLibriSpeechPlaybackManifest(
    std::string_view json);

[[nodiscard]] LibriSpeechPlaybackManifestLoadResult loadLibriSpeechPlaybackManifest(
    const std::filesystem::path& manifestPath);

} // namespace Voice2VocalSynth
