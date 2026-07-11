#include <Voice2VocalSynth/LinuxVirtualAudioCli.h>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace
{
using namespace Voice2VocalSynth;

std::filesystem::path repositoryRoot()
{
#ifdef VOICE2VOCALSYNTH_REPOSITORY_ROOT
    return std::filesystem::path{VOICE2VOCALSYNTH_REPOSITORY_ROOT};
#else
    return std::filesystem::current_path();
#endif
}

std::filesystem::path tempRoot()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("voice2vocalsynth-linux-audio-cli-" + std::to_string(stamp));
}

std::filesystem::path fixtureReportPath()
{
    return repositoryRoot() / "tests/fixtures/linux-audio/check_report_ok.json";
}

void parsesCheckReportArgs()
{
    std::string error;
    const auto options = parseLinuxVirtualAudioCliArgs(
        {"Voice2VocalSynthLinuxAudioValidate",
         "--parse-check-report",
         "--report",
         "/tmp/check.json",
         "--verify-root",
         "/tmp/verify",
         "--manifest",
         "/tmp/custom-manifest.json"},
        error);
    assert(options);
    assert(error.empty());
    assert(options->action == LinuxVirtualAudioCliAction::ParseCheckReport);
    assert(options->reportPath == "/tmp/check.json");
    assert(options->verifyRoot == "/tmp/verify");
    assert(options->manifestPath == "/tmp/custom-manifest.json");
}

void rejectsMissingReport()
{
    std::string error;
    assert(!parseLinuxVirtualAudioCliArgs(
        {"Voice2VocalSynthLinuxAudioValidate", "--parse-check-report"}, error));
    assert(!error.empty());
}

void parsesValidCheckReport()
{
    LinuxVirtualAudioCliOptions options;
    options.action = LinuxVirtualAudioCliAction::ParseCheckReport;
    options.reportPath = fixtureReportPath();

    const auto result = runLinuxVirtualAudioCli(options);
    assert(result.exitCode == LinuxVirtualAudioCliExitCode::Success);
    assert(result.message.find("pipewire-loopback") != std::string::npos);
    assert(result.message.find("LivePhonemeVerify.monitor") != std::string::npos);
}

void writesManifestFromCheckReport()
{
    const auto root = tempRoot();
    LinuxVirtualAudioCliOptions options;
    options.action = LinuxVirtualAudioCliAction::WriteManifest;
    options.verifyRoot = root;
    options.reportPath = fixtureReportPath();
    options.manifestPath = defaultLinuxVirtualAudioManifestPath(root);

    const auto result = runLinuxVirtualAudioCli(options);
    assert(result.exitCode == LinuxVirtualAudioCliExitCode::Success);
    assert(std::filesystem::exists(options.manifestPath));

    std::ifstream manifest(options.manifestPath);
    const std::string text((std::istreambuf_iterator<char>(manifest)), std::istreambuf_iterator<char>());
    assert(text.find("LivePhonemeVerify") != std::string::npos);
    assert(text.find("pipewire-loopback") != std::string::npos);

    std::filesystem::remove_all(root);
}

void rejectsInvalidCheckReport()
{
    const auto root = tempRoot();
    const auto reportPath = root / "invalid.json";
    std::ofstream(reportPath) << "{\"valid\": false, \"error\": \"fixture failure\"}";

    LinuxVirtualAudioCliOptions options;
    options.action = LinuxVirtualAudioCliAction::ParseCheckReport;
    options.reportPath = reportPath;

    const auto result = runLinuxVirtualAudioCli(options);
    assert(result.exitCode == LinuxVirtualAudioCliExitCode::RuntimeError);
    assert(!result.message.empty());

    std::filesystem::remove_all(root);
}

} // namespace

int main()
{
    parsesCheckReportArgs();
    rejectsMissingReport();
    parsesValidCheckReport();
    writesManifestFromCheckReport();
    rejectsInvalidCheckReport();
    std::cout << "LinuxVirtualAudioCli tests passed\n";
    return 0;
}
