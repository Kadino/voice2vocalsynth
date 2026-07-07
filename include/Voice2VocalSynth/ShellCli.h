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
};

struct ShellLiveLogExportPaths
{
    LivePhonemeVerifyRunPaths runPaths;
};

[[nodiscard]] std::string shellCliUsage();

/// Parses JUCE shell CLI flags related to live JSONL export.
[[nodiscard]] std::optional<ShellLiveLogExportOptions> parseShellLiveLogExportArgs(
    const std::vector<std::string>& args,
    std::string& error);

/// Resolves export paths: auto-creates a timestamped run unless `--live-log-out` overrides.
[[nodiscard]] bool resolveShellLiveLogExportPaths(const ShellLiveLogExportOptions& options,
                                                  ShellLiveLogExportPaths& out,
                                                  std::string& error);

} // namespace Voice2VocalSynth
