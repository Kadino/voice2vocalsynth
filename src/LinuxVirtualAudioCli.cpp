#include "Voice2VocalSynth/LinuxVirtualAudioCli.h"

#include <fstream>
#include <sstream>

namespace Voice2VocalSynth
{
namespace
{

[[nodiscard]] bool requireValue(const std::vector<std::string>& args,
                                std::size_t& index,
                                std::string& error)
{
    if (index + 1 >= args.size()) {
        error = "Missing value for " + args[index];
        return false;
    }
    ++index;
    return true;
}

[[nodiscard]] std::optional<std::string> readFileToString(const std::filesystem::path& path,
                                                          std::string& error)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Unable to read file: " + path.string();
        return std::nullopt;
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

} // namespace

std::string linuxVirtualAudioCliUsage()
{
    return "Voice2VocalSynthLinuxAudioValidate "
           "--parse-check-report --report <check-report.json> "
           "| --write-manifest --report <check-report.json> "
           "[--verify-root <LivePhonemeVerifyRoot>] [--manifest <path>]";
}

std::optional<LinuxVirtualAudioCliOptions> parseLinuxVirtualAudioCliArgs(
    const std::vector<std::string>& args,
    std::string& error)
{
    LinuxVirtualAudioCliOptions options;
    bool haveAction = false;
    error.clear();

    for (std::size_t index = 1; index < args.size(); ++index) {
        const auto& arg = args[index];
        if (arg == "--parse-check-report") {
            options.action = LinuxVirtualAudioCliAction::ParseCheckReport;
            haveAction = true;
            continue;
        }
        if (arg == "--write-manifest") {
            options.action = LinuxVirtualAudioCliAction::WriteManifest;
            haveAction = true;
            continue;
        }
        if (arg == "--report") {
            if (!requireValue(args, index, error)) {
                return std::nullopt;
            }
            options.reportPath = args[index];
            continue;
        }
        if (arg == "--verify-root") {
            if (!requireValue(args, index, error)) {
                return std::nullopt;
            }
            options.verifyRoot = args[index];
            options.manifestPath = defaultLinuxVirtualAudioManifestPath(options.verifyRoot);
            continue;
        }
        if (arg == "--manifest") {
            if (!requireValue(args, index, error)) {
                return std::nullopt;
            }
            options.manifestPath = args[index];
            continue;
        }
        error = "Unknown argument: " + arg;
        return std::nullopt;
    }

    if (!haveAction) {
        error = "Missing required action";
        return std::nullopt;
    }
    if (options.reportPath.empty()) {
        error = "--report is required";
        return std::nullopt;
    }
    return options;
}

LinuxVirtualAudioCliResult runLinuxVirtualAudioCli(const LinuxVirtualAudioCliOptions& options)
{
    LinuxVirtualAudioCliResult result;
    std::string error;
    const auto reportText = readFileToString(options.reportPath, error);
    if (!reportText) {
        result.exitCode = LinuxVirtualAudioCliExitCode::RuntimeError;
        result.message = error;
        return result;
    }

    const auto parsed = parseLinuxVirtualAudioCheckReport(*reportText);
    if (!parsed.ok) {
        result.exitCode = LinuxVirtualAudioCliExitCode::RuntimeError;
        result.message = parsed.error;
        return result;
    }

    if (options.action == LinuxVirtualAudioCliAction::ParseCheckReport) {
        if (!parsed.validation.valid) {
            result.exitCode = LinuxVirtualAudioCliExitCode::RuntimeError;
            result.message = parsed.validation.error.empty()
                                 ? "Linux virtual audio validation failed"
                                 : parsed.validation.error;
            return result;
        }

        std::ostringstream message;
        message << "Linux virtual audio validation passed\n"
                << "route: " << parsed.validation.route.routeId << '\n'
                << "playback: " << parsed.validation.route.playbackDevice << '\n'
                << "capture: " << parsed.validation.route.captureDevice << '\n'
                << "server: " << parsed.validation.route.serverName << '\n'
                << "probe: " << (parsed.validation.probePassed ? "passed" : "skipped");
        result.exitCode = LinuxVirtualAudioCliExitCode::Success;
        result.message = message.str();
        return result;
    }

    if (!parsed.validation.valid) {
        result.exitCode = LinuxVirtualAudioCliExitCode::RuntimeError;
        result.message = "Refusing to write manifest for invalid check report: " +
                         parsed.validation.error;
        return result;
    }

    LinuxVirtualAudioManifestInfo manifest;
    manifest.route = parsed.validation.route;
    manifest.probePassed = parsed.validation.probePassed;
    manifest.warnings = parsed.validation.warnings;
    if (!writeLinuxVirtualAudioManifest(manifest, options.manifestPath, error)) {
        result.exitCode = LinuxVirtualAudioCliExitCode::RuntimeError;
        result.message = error;
        return result;
    }

    result.exitCode = LinuxVirtualAudioCliExitCode::Success;
    result.message = "Wrote Linux virtual audio manifest: " + options.manifestPath.string();
    return result;
}

} // namespace Voice2VocalSynth
