#include <Voice2VocalSynth/ShellCli.h>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>

namespace
{
using namespace Voice2VocalSynth;

std::filesystem::path tempVerifyRoot()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("voice2vocalsynth-shell-cli-" + std::to_string(stamp));
}

void parsesExportFlags()
{
    std::string error;
    const std::vector<std::string> args {
        "Voice2VocalSynthApp",
        "--live-log-export",
        "--live-log-out",
        "/tmp/custom/live-log.jsonl",
        "--phoneme-backend",
        "pocketsphinx",
        "--pocketsphinx-model-root",
        "/tmp/model",
        "--capture-device",
        "LivePhonemeVerify.monitor",
        "--quit-after-seconds",
        "20",
        "--quit-file",
        "/tmp/custom/quit",
        "--auto-loopback-measure",
    };
    const auto options = parseShellLiveLogExportArgs(args, error);
    assert(options);
    assert(options->enabled);
    assert(options->outputOverride == "/tmp/custom/live-log.jsonl");
    assert(options->phonemeBackend == "pocketsphinx");
    assert(options->pocketSphinxModelRoot == "/tmp/model");
    assert(options->captureDevice == "LivePhonemeVerify.monitor");
    assert(options->quitAfterSeconds == 20.0);
    assert(options->quitFile == "/tmp/custom/quit");
    assert(options->autoLoopbackMeasure);
    assert(error.empty());
}

void rejectsInvalidAutomationOptions()
{
    std::string error;
    assert(!parseShellLiveLogExportArgs(
        {"Voice2VocalSynthApp", "--phoneme-backend", "unknown"}, error));
    assert(!error.empty());
    assert(!parseShellLiveLogExportArgs(
        {"Voice2VocalSynthApp", "--quit-after-seconds", "0"}, error));
    assert(!error.empty());
    assert(!parseShellLiveLogExportArgs(
        {"Voice2VocalSynthApp", "--quit-after-seconds", "nan"}, error));
    assert(!error.empty());
}

void rejectsOutputWithoutExport()
{
    std::string error;
    const std::vector<std::string> args {
        "Voice2VocalSynthApp",
        "--live-log-out",
        "/tmp/custom/live-log.jsonl",
    };
    assert(!parseShellLiveLogExportArgs(args, error));
    assert(!error.empty());
}

void resolvesOverrideFilePath()
{
    const auto root = tempVerifyRoot() / "custom";
    const auto file = root / "capture.jsonl";
    std::string error;
    ShellLiveLogExportOptions options;
    options.enabled = true;
    options.outputOverride = file;
    ShellLiveLogExportPaths paths;
    assert(resolveShellLiveLogExportPaths(options, paths, error));
    assert(error.empty());
    assert(paths.runPaths.liveLog == file);
    assert(std::filesystem::exists(root));

    std::filesystem::remove_all(root.parent_path());
}

} // namespace

int main()
{
    parsesExportFlags();
    rejectsOutputWithoutExport();
    resolvesOverrideFilePath();
    rejectsInvalidAutomationOptions();
    std::cout << "ShellCli tests passed\n";
    return 0;
}
