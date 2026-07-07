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
    };
    const auto options = parseShellLiveLogExportArgs(args, error);
    assert(options);
    assert(options->enabled);
    assert(options->outputOverride == "/tmp/custom/live-log.jsonl");
    assert(error.empty());
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
    std::cout << "ShellCli tests passed\n";
    return 0;
}
