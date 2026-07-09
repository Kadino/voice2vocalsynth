#pragma once

#include "Voice2VocalSynth/LinuxVirtualAudio.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Voice2VocalSynth
{

enum class LinuxVirtualAudioCliExitCode
{
    Success = 0,
    UsageError = 1,
    RuntimeError = 2,
};

enum class LinuxVirtualAudioCliAction
{
    ParseCheckReport,
    WriteManifest,
};

struct LinuxVirtualAudioCliOptions
{
    LinuxVirtualAudioCliAction action = LinuxVirtualAudioCliAction::ParseCheckReport;
    std::filesystem::path verifyRoot = defaultLivePhonemeVerifyRoot();
    std::filesystem::path reportPath;
    std::filesystem::path manifestPath = defaultLinuxVirtualAudioManifestPath();
};

[[nodiscard]] std::string linuxVirtualAudioCliUsage();

[[nodiscard]] std::optional<LinuxVirtualAudioCliOptions> parseLinuxVirtualAudioCliArgs(
    const std::vector<std::string>& args,
    std::string& error);

struct LinuxVirtualAudioCliResult
{
    LinuxVirtualAudioCliExitCode exitCode = LinuxVirtualAudioCliExitCode::RuntimeError;
    std::string message;
};

[[nodiscard]] LinuxVirtualAudioCliResult runLinuxVirtualAudioCli(
    const LinuxVirtualAudioCliOptions& options);

} // namespace Voice2VocalSynth
