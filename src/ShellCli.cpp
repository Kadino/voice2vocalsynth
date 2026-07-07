#include "Voice2VocalSynth/ShellCli.h"

namespace Voice2VocalSynth
{
namespace
{

[[nodiscard]] bool createDirectory(const std::filesystem::path& path, std::string& error)
{
    std::error_code ec;
    if (std::filesystem::create_directories(path, ec) || std::filesystem::exists(path)) {
        return true;
    }
    error = "Unable to create directory: " + path.string() + " (" + ec.message() + ")";
    return false;
}

[[nodiscard]] bool pathLooksLikeJsonlFile(const std::filesystem::path& path)
{
    const auto extension = path.extension().string();
    return extension == ".jsonl" || extension == ".json";
}

[[nodiscard]] bool pathIsExistingDirectory(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec) && std::filesystem::is_directory(path, ec);
}

[[nodiscard]] bool resolveOverridePaths(const std::filesystem::path& overridePath,
                                        LivePhonemeVerifyRunPaths& out,
                                        std::string& error)
{
    error.clear();
    const auto normalized = overridePath.lexically_normal();
    const bool treatAsDirectory = normalized.filename().empty() || pathIsExistingDirectory(normalized)
                                  || (!pathLooksLikeJsonlFile(normalized)
                                      && !std::filesystem::exists(normalized));

    if (treatAsDirectory) {
        out.runDirectory = normalized;
        out.liveLog = out.runDirectory / "live-log.jsonl";
        out.manifest = out.runDirectory / "manifest.json";
    } else {
        out.liveLog = normalized;
        out.runDirectory = out.liveLog.parent_path();
        out.manifest = out.runDirectory / "manifest.json";
    }

    if (out.runDirectory.empty()) {
        error = "Unable to resolve live log export directory from: " + overridePath.string();
        return false;
    }
    if (!createDirectory(out.runDirectory, error)) {
        return false;
    }
    return true;
}

} // namespace

std::string shellCliUsage()
{
    return "Voice2VocalSynthApp [--live-log-export] [--live-log-out <path>]";
}

std::optional<ShellLiveLogExportOptions> parseShellLiveLogExportArgs(const std::vector<std::string>& args,
                                                                     std::string& error)
{
    ShellLiveLogExportOptions options;
    error.clear();

    for (std::size_t index = 1; index < args.size(); ++index) {
        const auto& arg = args[index];
        if (arg == "--live-log-export") {
            options.enabled = true;
            continue;
        }
        if (arg == "--live-log-out") {
            if (index + 1 >= args.size()) {
                error = "Missing value for --live-log-out";
                return std::nullopt;
            }
            options.outputOverride = args[++index];
            continue;
        }
        error = "Unknown argument: " + arg;
        return std::nullopt;
    }

    if (options.outputOverride && !options.enabled) {
        error = "--live-log-out requires --live-log-export";
        return std::nullopt;
    }

    return options;
}

bool resolveShellLiveLogExportPaths(const ShellLiveLogExportOptions& options,
                                    ShellLiveLogExportPaths& out,
                                    std::string& error)
{
    error.clear();
    if (!options.enabled) {
        error = "Live log export is not enabled";
        return false;
    }

    if (options.outputOverride) {
        return resolveOverridePaths(*options.outputOverride, out.runPaths, error);
    }

    return createLivePhonemeVerifyRun(defaultLivePhonemeVerifyRoot(), out.runPaths, error);
}

} // namespace Voice2VocalSynth
