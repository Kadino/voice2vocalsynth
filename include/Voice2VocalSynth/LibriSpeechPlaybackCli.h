#pragma once

#include "Voice2VocalSynth/LibriSpeechPlayback.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Voice2VocalSynth
{

enum class LibriSpeechPlaybackCliExitCode
{
    Success = 0,
    UsageError = 1,
    RuntimeError = 2,
};

enum class LibriSpeechPlaybackCliAction
{
    BuildManifest,
};

struct LibriSpeechPlaybackCliOptions
{
    LibriSpeechPlaybackCliAction action = LibriSpeechPlaybackCliAction::BuildManifest;
    std::filesystem::path verifyRoot = defaultLivePhonemeVerifyRoot();
    std::filesystem::path datasetRoot;
    std::optional<std::filesystem::path> runDirectoryOverride;
    std::optional<std::filesystem::path> manifestPathOverride;
    std::optional<std::string> utteranceId;
    std::size_t subsetCount = 20;
    bool allMode = false;
    double gapSeconds = kDefaultLibriSpeechPlaybackGapSeconds;
    std::optional<std::string> playbackDeviceOverride;
    std::filesystem::path durationsTsv;
};

[[nodiscard]] std::string libriSpeechPlaybackCliUsage();

[[nodiscard]] std::optional<LibriSpeechPlaybackCliOptions> parseLibriSpeechPlaybackCliArgs(
    const std::vector<std::string>& args,
    std::string& error);

struct LibriSpeechPlaybackCliResult
{
    LibriSpeechPlaybackCliExitCode exitCode = LibriSpeechPlaybackCliExitCode::RuntimeError;
    std::string message;
};

[[nodiscard]] LibriSpeechPlaybackCliResult runLibriSpeechPlaybackCli(
    const LibriSpeechPlaybackCliOptions& options);

} // namespace Voice2VocalSynth
