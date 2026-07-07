#pragma once

#include "Voice2VocalSynth/LibriSpeechDataset.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Voice2VocalSynth
{

enum class LibriSpeechDatasetCliExitCode
{
    Success = 0,
    UsageError = 1,
    RuntimeError = 2,
};

enum class LibriSpeechDatasetCliAction
{
    Discover,
    Verify,
    WriteManifest,
};

struct LibriSpeechDatasetCliOptions
{
    LibriSpeechDatasetCliAction action = LibriSpeechDatasetCliAction::Verify;
    std::filesystem::path verifyRoot = defaultLivePhonemeVerifyRoot();
    std::optional<std::filesystem::path> datasetRootOverride;
    std::optional<std::filesystem::path> manifestPathOverride;
};

[[nodiscard]] std::string libriSpeechDatasetCliUsage();

[[nodiscard]] std::optional<LibriSpeechDatasetCliOptions> parseLibriSpeechDatasetCliArgs(
    const std::vector<std::string>& args,
    std::string& error);

[[nodiscard]] std::string formatLibriSpeechDatasetSummary(const LibriSpeechTestCleanSummary& summary);

struct LibriSpeechDatasetCliResult
{
    LibriSpeechDatasetCliExitCode exitCode = LibriSpeechDatasetCliExitCode::RuntimeError;
    std::string message;
};

[[nodiscard]] LibriSpeechDatasetCliResult runLibriSpeechDatasetCli(
    const LibriSpeechDatasetCliOptions& options);

} // namespace Voice2VocalSynth
