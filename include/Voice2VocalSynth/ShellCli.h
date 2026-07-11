#pragma once

#include "Voice2VocalSynth/LivePhonemeVerifyPaths.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Voice2VocalSynth
{

struct ShellLiveLogExportOptions
{
    bool enabled = false;
    std::optional<std::filesystem::path> outputOverride;
    std::optional<std::string> phonemeBackend;
    std::optional<std::filesystem::path> pocketSphinxModelRoot;
    std::optional<std::filesystem::path> onnxModelPath;
    std::optional<std::filesystem::path> onnxConfigPath;
    std::optional<std::string> captureDevice;
    std::optional<double> quitAfterSeconds;
    std::optional<std::filesystem::path> quitFile;
    bool autoLoopbackMeasure = false;
};

struct ShellLiveLogExportPaths
{
    LivePhonemeVerifyRunPaths runPaths;
};

[[nodiscard]] std::string shellCliUsage();

/// Parses JUCE shell automation and live JSONL export flags.
[[nodiscard]] std::optional<ShellLiveLogExportOptions> parseShellLiveLogExportArgs(
    const std::vector<std::string>& args,
    std::string& error);

/// Resolves export paths: auto-creates a timestamped run unless `--live-log-out` overrides.
[[nodiscard]] bool resolveShellLiveLogExportPaths(const ShellLiveLogExportOptions& options,
                                                  ShellLiveLogExportPaths& out,
                                                  std::string& error);

} // namespace Voice2VocalSynth
