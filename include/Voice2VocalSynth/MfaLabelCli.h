#pragma once

#include "Voice2VocalSynth/MfaLabelPipeline.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Voice2VocalSynth
{

enum class MfaLabelCliExitCode
{
    Success = 0,
    UsageError = 1,
    RuntimeError = 2,
};

enum class MfaLabelCliAction
{
    ConvertTextGrids,
    WriteManifest,
};

struct MfaLabelCliOptions
{
    MfaLabelCliAction action = MfaLabelCliAction::ConvertTextGrids;
    std::filesystem::path verifyRoot = defaultLivePhonemeVerifyRoot();
    std::filesystem::path textGridRoot;
    std::filesystem::path labelsRoot = defaultLibriSpeechLabelsRoot();
    std::filesystem::path manifestPath = defaultLibriSpeechLabelManifestPath();
    std::filesystem::path datasetRoot;
    std::string mfaVersion;
    std::string subsetMode;
    std::size_t utteranceCount = 0;
    std::size_t labelFileCount = 0;
};

[[nodiscard]] std::string mfaLabelCliUsage();

[[nodiscard]] std::optional<MfaLabelCliOptions> parseMfaLabelCliArgs(
    const std::vector<std::string>& args,
    std::string& error);

struct MfaLabelCliResult
{
    MfaLabelCliExitCode exitCode = MfaLabelCliExitCode::RuntimeError;
    std::string message;
};

[[nodiscard]] MfaLabelCliResult runMfaLabelCli(const MfaLabelCliOptions& options);

} // namespace Voice2VocalSynth
