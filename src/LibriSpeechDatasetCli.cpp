#include "Voice2VocalSynth/LibriSpeechDatasetCli.h"

#include <sstream>

namespace Voice2VocalSynth
{
namespace
{

[[nodiscard]] std::optional<std::filesystem::path> resolveDatasetRoot(
    const LibriSpeechDatasetCliOptions& options,
    std::string& error)
{
    if (options.datasetRootOverride) {
        const auto validation = validateLibriSpeechTestClean(*options.datasetRootOverride);
        if (!validation.valid) {
            error = validation.error;
            return std::nullopt;
        }
        return *options.datasetRootOverride;
    }

    std::string note;
    const auto discovered = discoverLibriSpeechTestCleanRoot(options.verifyRoot, note);
    if (!discovered) {
        error = note;
        return std::nullopt;
    }
    return discovered;
}

} // namespace

std::string libriSpeechDatasetCliUsage()
{
    return "Voice2VocalSynthLibriSpeechSetup "
           "[--discover | --verify | --write-manifest] "
           "[--verify-root <LivePhonemeVerifyRoot>] "
           "[--dataset-root <LibriSpeechTestCleanRoot>] "
           "[--manifest <manifest.json>]";
}

std::optional<LibriSpeechDatasetCliOptions> parseLibriSpeechDatasetCliArgs(
    const std::vector<std::string>& args,
    std::string& error)
{
    LibriSpeechDatasetCliOptions options;
    bool haveAction = false;
    error.clear();

    for (std::size_t index = 1; index < args.size(); ++index) {
        const auto& arg = args[index];
        if (arg == "--discover") {
            options.action = LibriSpeechDatasetCliAction::Discover;
            haveAction = true;
            continue;
        }
        if (arg == "--verify") {
            options.action = LibriSpeechDatasetCliAction::Verify;
            haveAction = true;
            continue;
        }
        if (arg == "--write-manifest") {
            options.action = LibriSpeechDatasetCliAction::WriteManifest;
            haveAction = true;
            continue;
        }
        if (arg == "--verify-root") {
            if (index + 1 >= args.size()) {
                error = "Missing value for --verify-root";
                return std::nullopt;
            }
            options.verifyRoot = args[++index];
            continue;
        }
        if (arg == "--dataset-root") {
            if (index + 1 >= args.size()) {
                error = "Missing value for --dataset-root";
                return std::nullopt;
            }
            options.datasetRootOverride = args[++index];
            continue;
        }
        if (arg == "--manifest") {
            if (index + 1 >= args.size()) {
                error = "Missing value for --manifest";
                return std::nullopt;
            }
            options.manifestPathOverride = args[++index];
            continue;
        }
        error = "Unknown argument: " + arg;
        return std::nullopt;
    }

    if (!haveAction) {
        options.action = LibriSpeechDatasetCliAction::Verify;
    }
    return options;
}

std::string formatLibriSpeechDatasetSummary(const LibriSpeechTestCleanSummary& summary)
{
    std::ostringstream out;
    out << "LibriSpeech test-clean root: " << summary.root.string() << '\n'
        << "speakers: " << summary.speakerCount << '\n'
        << "chapters: " << summary.chapterCount << '\n'
        << "flac files: " << summary.flacCount << '\n'
        << "transcript files: " << summary.transcriptFileCount;
    return out.str();
}

LibriSpeechDatasetCliResult runLibriSpeechDatasetCli(const LibriSpeechDatasetCliOptions& options)
{
    LibriSpeechDatasetCliResult result;
    std::string error;

    if (options.action == LibriSpeechDatasetCliAction::Discover) {
        std::string note;
        const auto discovered = discoverLibriSpeechTestCleanRoot(options.verifyRoot, note);
        if (!discovered) {
            result.exitCode = LibriSpeechDatasetCliExitCode::RuntimeError;
            result.message = note;
            return result;
        }
        const auto validation = validateLibriSpeechTestClean(*discovered);
        result.exitCode = LibriSpeechDatasetCliExitCode::Success;
        result.message = note + "\n" + formatLibriSpeechDatasetSummary(validation.summary);
        return result;
    }

    const auto datasetRoot = resolveDatasetRoot(options, error);
    if (!datasetRoot) {
        result.exitCode = LibriSpeechDatasetCliExitCode::RuntimeError;
        result.message = error;
        return result;
    }

    const auto validation = validateLibriSpeechTestClean(*datasetRoot);
    if (!validation.valid) {
        result.exitCode = LibriSpeechDatasetCliExitCode::RuntimeError;
        result.message = validation.error;
        return result;
    }

    if (options.action == LibriSpeechDatasetCliAction::Verify) {
        result.exitCode = LibriSpeechDatasetCliExitCode::Success;
        result.message = "LibriSpeech test-clean validation passed.\n"
                         + formatLibriSpeechDatasetSummary(validation.summary);
        return result;
    }

    const auto manifestPath = options.manifestPathOverride
                                  ? *options.manifestPathOverride
                                  : defaultLibriSpeechDatasetManifestPath(options.verifyRoot);
    if (!writeLibriSpeechDatasetManifest(validation.summary, manifestPath, error)) {
        result.exitCode = LibriSpeechDatasetCliExitCode::RuntimeError;
        result.message = error;
        return result;
    }

    result.exitCode = LibriSpeechDatasetCliExitCode::Success;
    result.message = "Wrote dataset manifest: " + manifestPath.string() + "\n"
                     + formatLibriSpeechDatasetSummary(validation.summary);
    return result;
}

} // namespace Voice2VocalSynth
