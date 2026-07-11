#pragma once

#include <string>
#include <vector>

namespace Voice2VocalSynth
{

enum class LiveLogFixtureCliExitCode
{
    Success = 0,
    Usage = 1,
    RuntimeError = 2,
};

struct LiveLogFixtureCliResult
{
    LiveLogFixtureCliExitCode exitCode = LiveLogFixtureCliExitCode::Usage;
    std::string message;
};

/// Headless JUCE-shell substitute for local verification phases 5–7.
[[nodiscard]] LiveLogFixtureCliResult runLiveLogFixtureCli(const std::vector<std::string>& args);

} // namespace Voice2VocalSynth
