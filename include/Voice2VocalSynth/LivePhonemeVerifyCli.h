#pragma once

#include <string>
#include <vector>

namespace Voice2VocalSynth
{

enum class LivePhonemeVerifyCliExitCode
{
    Success = 0,
    Usage = 1,
    RuntimeError = 2,
    GateFailed = 3,
};

struct LivePhonemeVerifyCliResult
{
    LivePhonemeVerifyCliExitCode exitCode = LivePhonemeVerifyCliExitCode::Usage;
    std::string message;
};

[[nodiscard]] std::string livePhonemeVerifyCliUsage();
[[nodiscard]] LivePhonemeVerifyCliResult runLivePhonemeVerifyCli(
    const std::vector<std::string>& args);

} // namespace Voice2VocalSynth
