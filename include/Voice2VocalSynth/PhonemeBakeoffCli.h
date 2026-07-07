#pragma once

#include "Voice2VocalSynth/EvalDataPaths.h"
#include "Voice2VocalSynth/PhonemeBakeoff.h"
#include "Voice2VocalSynth/PhonemeEvaluation.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Voice2VocalSynth
{

enum class PhonemeBakeoffCliExitCode
{
    Success = 0,
    UsageError = 1,
    RuntimeError = 2,
};

struct PhonemeBakeoffCliOptions
{
    std::filesystem::path referencePath;
    std::filesystem::path audioPath;
    std::optional<std::filesystem::path> evalDataRoot;
    std::optional<std::string> clipName;
    std::vector<std::string> backendNames;
    std::optional<std::filesystem::path> onnxModelPath;
    std::optional<std::filesystem::path> onnxConfigPath;
    std::optional<std::filesystem::path> outputPath;
    bool allClips = false;
    PhonemeEvaluationOptions evalOptions;
};

struct PhonemeBakeoffCliResult
{
    PhonemeBakeoffCliExitCode exitCode = PhonemeBakeoffCliExitCode::RuntimeError;
    std::string summary;
    std::optional<std::string> reportJson;
};

[[nodiscard]] std::string phonemeBakeoffCliUsage();

[[nodiscard]] std::optional<PhonemeBakeoffCliOptions> parsePhonemeBakeoffCliArgs(
    const std::vector<std::string>& args,
    std::string& error);

[[nodiscard]] PhonemeBakeoffCliResult runPhonemeBakeoffCli(const PhonemeBakeoffCliOptions& options);

} // namespace Voice2VocalSynth
