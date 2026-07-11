#include "Voice2VocalSynth/ShellCli.h"

#include <cmath>
#include <exception>
#include <stdexcept>

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
        out = livePhonemeVerifyRunPaths(normalized);
    } else {
        out = livePhonemeVerifyRunPaths(normalized.parent_path());
        out.liveLog = normalized;
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
    return "Voice2VocalSynthApp [--live-log-export] [--live-log-out <path>] "
           "[--phoneme-backend placeholder|onnx_phoneme|pocketsphinx] "
           "[--pocketsphinx-model-root <path>] [--onnx-model <path>] "
           "[--onnx-config <path>] [--capture-device <name>] "
           "[--auto-loopback-measure] [--quit-after-seconds <seconds>] "
           "[--quit-file <path>]";
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
        if (arg == "--phoneme-backend") {
            if (index + 1 >= args.size()) {
                error = "Missing value for --phoneme-backend";
                return std::nullopt;
            }
            const auto& backend = args[++index];
            if (backend != "placeholder" && backend != "onnx_phoneme" &&
                backend != "pocketsphinx") {
                error = "Unsupported phoneme backend: " + backend;
                return std::nullopt;
            }
            options.phonemeBackend = backend;
            continue;
        }
        if (arg == "--pocketsphinx-model-root" || arg == "--onnx-model" ||
            arg == "--onnx-config") {
            if (index + 1 >= args.size()) {
                error = "Missing value for " + arg;
                return std::nullopt;
            }
            const std::filesystem::path value = args[++index];
            if (arg == "--pocketsphinx-model-root") {
                options.pocketSphinxModelRoot = value;
            } else if (arg == "--onnx-model") {
                options.onnxModelPath = value;
            } else {
                options.onnxConfigPath = value;
            }
            continue;
        }
        if (arg == "--capture-device") {
            if (index + 1 >= args.size()) {
                error = "Missing value for --capture-device";
                return std::nullopt;
            }
            options.captureDevice = args[++index];
            continue;
        }
        if (arg == "--quit-file") {
            if (index + 1 >= args.size()) {
                error = "Missing value for --quit-file";
                return std::nullopt;
            }
            options.quitFile = args[++index];
            continue;
        }
        if (arg == "--quit-after-seconds") {
            if (index + 1 >= args.size()) {
                error = "Missing value for --quit-after-seconds";
                return std::nullopt;
            }
            try {
                const auto& value = args[++index];
                std::size_t consumed = 0;
                options.quitAfterSeconds = std::stod(value, &consumed);
                if (consumed != value.size()) {
                    throw std::invalid_argument("trailing content");
                }
            } catch (const std::exception&) {
                error = "Invalid value for --quit-after-seconds";
                return std::nullopt;
            }
            if (!std::isfinite(*options.quitAfterSeconds) ||
                *options.quitAfterSeconds <= 0.0) {
                error = "--quit-after-seconds must be positive";
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--auto-loopback-measure") {
            options.autoLoopbackMeasure = true;
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
