#include "Voice2VocalSynth/LiveLogFixtureCli.h"

#include "Voice2VocalSynth/LiveLogFixture.h"
#include "Voice2VocalSynth/LivePhonemeVerifyPaths.h"
#include "Voice2VocalSynth/ShellCli.h"

#include <chrono>
#include <filesystem>
#include <thread>

namespace Voice2VocalSynth
{
namespace
{

[[nodiscard]] bool waitForQuitFile(const std::filesystem::path& quitFile)
{
    for (int attempt = 0; attempt < 1500; ++attempt) {
        std::error_code ec;
        if (std::filesystem::exists(quitFile, ec)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return false;
}

[[nodiscard]] bool waitForQuitAfterSeconds(const double seconds)
{
    if (seconds <= 0.0) {
        return true;
    }
    std::this_thread::sleep_for(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::duration<double>(seconds)));
    return true;
}

} // namespace

LiveLogFixtureCliResult runLiveLogFixtureCli(const std::vector<std::string>& args)
{
    LiveLogFixtureCliResult result;
    std::string error;
    const auto options = parseShellLiveLogExportArgs(args, error);
    if (!options) {
        result.exitCode = LiveLogFixtureCliExitCode::Usage;
        result.message = error.empty() ? shellCliUsage() : error + "\n" + shellCliUsage();
        return result;
    }
    if (!options->enabled) {
        result.exitCode = LiveLogFixtureCliExitCode::Usage;
        result.message = "--live-log-export is required for the headless live-log fixture app";
        return result;
    }

    ShellLiveLogExportPaths exportPaths;
    if (!resolveShellLiveLogExportPaths(*options, exportPaths, error)) {
        result.exitCode = LiveLogFixtureCliExitCode::RuntimeError;
        result.message = error;
        return result;
    }
    if (!writeLivePhonemeVerifyManifest(exportPaths.runPaths, error)) {
        result.exitCode = LiveLogFixtureCliExitCode::RuntimeError;
        result.message = error;
        return result;
    }

    LiveLogFixtureOptions fixtureOptions;
    fixtureOptions.runPaths = exportPaths.runPaths;
    fixtureOptions.backendCli = options->phonemeBackend.value_or("pocketsphinx");
    fixtureOptions.captureDevice = options->captureDevice.value_or("LivePhonemeVerify.monitor");
    fixtureOptions.includeOnnxLatency = fixtureOptions.backendCli == "onnx_phoneme";
    fixtureOptions.includeLatencyMeasure = options->autoLoopbackMeasure;

    if (!writeLiveLogFixture(fixtureOptions, error)) {
        result.exitCode = LiveLogFixtureCliExitCode::RuntimeError;
        result.message = error;
        return result;
    }

    if (options->quitFile) {
        if (!waitForQuitFile(*options->quitFile)) {
            result.exitCode = LiveLogFixtureCliExitCode::RuntimeError;
            result.message = "Timed out waiting for quit file: " + options->quitFile->string();
            return result;
        }
    } else if (options->quitAfterSeconds) {
        if (!waitForQuitAfterSeconds(*options->quitAfterSeconds)) {
            result.exitCode = LiveLogFixtureCliExitCode::RuntimeError;
            result.message = "Quit-after-seconds wait failed";
            return result;
        }
    }

    result.exitCode = LiveLogFixtureCliExitCode::Success;
    result.message = "Headless live-log fixture wrote " + exportPaths.runPaths.liveLog.string();
    return result;
}

} // namespace Voice2VocalSynth
