#include "Voice2VocalSynth/MfaLabelCli.h"

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

} // namespace

std::string mfaLabelCliUsage()
{
    return "Voice2VocalSynthMfaLabelConvert "
           "--convert-textgrids --textgrid-root <dir> [--labels-root <dir>] "
           "| --write-manifest --mfa-version <version> --subset-mode <subset|all> "
           "--utterance-count <n> --label-file-count <n> "
           "[--verify-root <LivePhonemeVerifyRoot>] [--dataset-root <path>] [--manifest <path>]";
}

std::optional<MfaLabelCliOptions> parseMfaLabelCliArgs(const std::vector<std::string>& args,
                                                       std::string& error)
{
    MfaLabelCliOptions options;
    bool haveAction = false;
    error.clear();

    for (std::size_t index = 1; index < args.size(); ++index) {
        const auto& arg = args[index];
        if (arg == "--convert-textgrids") {
            options.action = MfaLabelCliAction::ConvertTextGrids;
            haveAction = true;
            continue;
        }
        if (arg == "--write-manifest") {
            options.action = MfaLabelCliAction::WriteManifest;
            haveAction = true;
            continue;
        }
        if (arg == "--textgrid-root") {
            if (!requireValue(args, index, error)) {
                return std::nullopt;
            }
            options.textGridRoot = args[index];
            continue;
        }
        if (arg == "--labels-root") {
            if (!requireValue(args, index, error)) {
                return std::nullopt;
            }
            options.labelsRoot = args[index];
            continue;
        }
        if (arg == "--verify-root") {
            if (!requireValue(args, index, error)) {
                return std::nullopt;
            }
            options.verifyRoot = args[index];
            options.labelsRoot = defaultLibriSpeechLabelsRoot(options.verifyRoot);
            options.manifestPath = defaultLibriSpeechLabelManifestPath(options.verifyRoot);
            continue;
        }
        if (arg == "--dataset-root") {
            if (!requireValue(args, index, error)) {
                return std::nullopt;
            }
            options.datasetRoot = args[index];
            continue;
        }
        if (arg == "--manifest") {
            if (!requireValue(args, index, error)) {
                return std::nullopt;
            }
            options.manifestPath = args[index];
            continue;
        }
        if (arg == "--mfa-version") {
            if (!requireValue(args, index, error)) {
                return std::nullopt;
            }
            options.mfaVersion = args[index];
            continue;
        }
        if (arg == "--subset-mode") {
            if (!requireValue(args, index, error)) {
                return std::nullopt;
            }
            options.subsetMode = args[index];
            continue;
        }
        if (arg == "--utterance-count") {
            if (!requireValue(args, index, error)) {
                return std::nullopt;
            }
            options.utteranceCount = static_cast<std::size_t>(std::stoull(args[index]));
            continue;
        }
        if (arg == "--label-file-count") {
            if (!requireValue(args, index, error)) {
                return std::nullopt;
            }
            options.labelFileCount = static_cast<std::size_t>(std::stoull(args[index]));
            continue;
        }
        error = "Unknown argument: " + arg;
        return std::nullopt;
    }

    if (!haveAction) {
        error = "Missing required action";
        return std::nullopt;
    }
    return options;
}

MfaLabelCliResult runMfaLabelCli(const MfaLabelCliOptions& options)
{
    MfaLabelCliResult result;
    std::string error;

    if (options.action == MfaLabelCliAction::ConvertTextGrids) {
        if (options.textGridRoot.empty()) {
            result.exitCode = MfaLabelCliExitCode::UsageError;
            result.message = "--textgrid-root is required for --convert-textgrids";
            return result;
        }

        MfaTextGridConversionSummary summary;
        if (!convertMfaTextGridTree(options.textGridRoot, options.labelsRoot, summary, error)) {
            result.exitCode = MfaLabelCliExitCode::RuntimeError;
            result.message = error;
            return result;
        }

        std::ostringstream message;
        message << "Converted TextGrids to label JSON under " << options.labelsRoot.string() << '\n'
                << "textgrids: " << summary.textGridCount << '\n'
                << "labels: " << summary.labelFileCount << '\n'
                << "skipped: " << summary.skippedTextGrids;
        result.exitCode = MfaLabelCliExitCode::Success;
        result.message = message.str();
        return result;
    }

    if (options.mfaVersion.empty() || options.subsetMode.empty()) {
        result.exitCode = MfaLabelCliExitCode::UsageError;
        result.message = "--mfa-version and --subset-mode are required for --write-manifest";
        return result;
    }

    MfaLabelManifestInfo manifest;
    manifest.mfaVersion = options.mfaVersion;
    manifest.subsetMode = options.subsetMode;
    manifest.utteranceCount = options.utteranceCount;
    manifest.labelFileCount = options.labelFileCount;
    manifest.labelsDirectory = options.labelsRoot;
    manifest.datasetRoot = options.datasetRoot;
    if (!writeMfaLabelManifest(manifest, options.manifestPath, error)) {
        result.exitCode = MfaLabelCliExitCode::RuntimeError;
        result.message = error;
        return result;
    }

    result.exitCode = MfaLabelCliExitCode::Success;
    result.message = "Wrote MFA label manifest: " + options.manifestPath.string();
    return result;
}

} // namespace Voice2VocalSynth
