#pragma once

#include "Voice2VocalSynth/PhonemeEvaluation.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Voice2VocalSynth
{

enum class PhonemeEvalCliExitCode
{
    Success = 0,
    UsageError = 1,
    RuntimeError = 2,
};

struct PhonemeEvalCliOptions
{
    std::filesystem::path referencePath;
    std::filesystem::path predictionPath;
    std::optional<std::filesystem::path> outputPath;
    PhonemeEvaluationOptions evalOptions;
};

struct PhonemeEvalCliResult
{
    PhonemeEvalCliExitCode exitCode = PhonemeEvalCliExitCode::RuntimeError;
    std::string summary;
    std::optional<std::string> metricsJson;
};

[[nodiscard]] std::string phonemeEvalCliUsage();

/// Parses CLI arguments. Returns `std::nullopt` on usage errors (message in `error`).
[[nodiscard]] std::optional<PhonemeEvalCliOptions> parsePhonemeEvalCliArgs(
    const std::vector<std::string>& args,
    std::string& error);

[[nodiscard]] std::string formatPhonemeEvaluationSummary(const PhonemeEvaluationMetrics& metrics);

[[nodiscard]] PhonemeEvalCliResult runPhonemeEvalCli(const PhonemeEvalCliOptions& options);

} // namespace Voice2VocalSynth
