#include "Voice2VocalSynth/LibriSpeechPlaybackCli.h"

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

std::string libriSpeechPlaybackCliUsage()
{
    return "Voice2VocalSynthLibriSpeechPlayback --build-manifest "
           "--durations-tsv <utterance-durations.tsv> "
           "[--verify-root <LivePhonemeVerifyRoot>] "
           "[--dataset-root <LibriSpeechTestCleanRoot>] "
           "[--run-dir <path>] [--manifest <playback-manifest.json>] "
           "[--subset N | --all | --utterance-id <id>] "
           "[--gap-seconds <seconds>] [--playback-device <device>]";
}

std::optional<LibriSpeechPlaybackCliOptions> parseLibriSpeechPlaybackCliArgs(
    const std::vector<std::string>& args,
    std::string& error)
{
    LibriSpeechPlaybackCliOptions options;
    bool haveAction = false;
    error.clear();

    for (std::size_t index = 1; index < args.size(); ++index) {
        const auto& arg = args[index];
        if (arg == "--build-manifest") {
            options.action = LibriSpeechPlaybackCliAction::BuildManifest;
            haveAction = true;
            continue;
        }
        if (arg == "--verify-root") {
            if (!requireValue(args, index, error)) {
                return std::nullopt;
            }
            options.verifyRoot = args[index];
            continue;
        }
        if (arg == "--dataset-root") {
            if (!requireValue(args, index, error)) {
                return std::nullopt;
            }
            options.datasetRoot = args[index];
            continue;
        }
        if (arg == "--run-dir") {
            if (!requireValue(args, index, error)) {
                return std::nullopt;
            }
            options.runDirectoryOverride = args[index];
            continue;
        }
        if (arg == "--manifest") {
            if (!requireValue(args, index, error)) {
                return std::nullopt;
            }
            options.manifestPathOverride = args[index];
            continue;
        }
        if (arg == "--durations-tsv") {
            if (!requireValue(args, index, error)) {
                return std::nullopt;
            }
            options.durationsTsv = args[index];
            continue;
        }
        if (arg == "--subset") {
            if (!requireValue(args, index, error)) {
                return std::nullopt;
            }
            options.subsetCount = static_cast<std::size_t>(std::stoull(args[index]));
            continue;
        }
        if (arg == "--all") {
            options.allMode = true;
            continue;
        }
        if (arg == "--utterance-id") {
            if (!requireValue(args, index, error)) {
                return std::nullopt;
            }
            options.utteranceId = args[index];
            continue;
        }
        if (arg == "--gap-seconds") {
            if (!requireValue(args, index, error)) {
                return std::nullopt;
            }
            options.gapSeconds = std::stod(args[index]);
            continue;
        }
        if (arg == "--playback-device") {
            if (!requireValue(args, index, error)) {
                return std::nullopt;
            }
            options.playbackDeviceOverride = args[index];
            continue;
        }
        error = "Unknown argument: " + arg;
        return std::nullopt;
    }

    if (!haveAction) {
        error = "Missing required action";
        return std::nullopt;
    }
    if (options.durationsTsv.empty()) {
        error = "--durations-tsv is required";
        return std::nullopt;
    }
    return options;
}

LibriSpeechPlaybackCliResult runLibriSpeechPlaybackCli(const LibriSpeechPlaybackCliOptions& options)
{
    LibriSpeechPlaybackCliResult result;
    std::string error;

    std::filesystem::path datasetRoot = options.datasetRoot;
    if (datasetRoot.empty()) {
        std::string note;
        const auto discovered = discoverLibriSpeechTestCleanRoot(options.verifyRoot, note);
        if (!discovered) {
            result.exitCode = LibriSpeechPlaybackCliExitCode::RuntimeError;
            result.message = note;
            return result;
        }
        datasetRoot = *discovered;
    } else {
        const auto validation = validateLibriSpeechTestClean(datasetRoot);
        if (!validation.valid) {
            result.exitCode = LibriSpeechPlaybackCliExitCode::RuntimeError;
            result.message = validation.error;
            return result;
        }
    }

    LinuxVirtualAudioRoute route;
    if (options.playbackDeviceOverride) {
        const auto audioManifestPath = defaultLinuxVirtualAudioManifestPath(options.verifyRoot);
        const auto loaded = loadLinuxVirtualAudioManifestFile(audioManifestPath);
        if (loaded.ok) {
            route = loaded.info.route;
        } else {
            route.routeId = std::string(kLinuxAudioRoutePipeWireLoopback);
            route.captureDevice = pactlMonitorSourceName(kLinuxAudioRecommendedSinkName);
            route.serverName = "override";
            route.note = "Playback device override without readable linux-virtual-audio.json";
        }
        route.playbackDevice = *options.playbackDeviceOverride;
    } else {
        const auto loaded =
            loadLinuxVirtualAudioManifestFile(defaultLinuxVirtualAudioManifestPath(options.verifyRoot));
        if (!loaded.ok) {
            result.exitCode = LibriSpeechPlaybackCliExitCode::RuntimeError;
            result.message = loaded.error +
                             ". Run scripts/validate_linux_virtual_audio.sh --write-manifest first.";
            return result;
        }
        route = loaded.info.route;
    }

    std::vector<LibriSpeechUtterance> utterances;
    if (options.utteranceId) {
        const auto all = listLibriSpeechUtterances(datasetRoot, 0);
        for (const auto& utterance : all) {
            if (utterance.id == *options.utteranceId) {
                utterances.push_back(utterance);
                break;
            }
        }
        if (utterances.empty()) {
            result.exitCode = LibriSpeechPlaybackCliExitCode::RuntimeError;
            result.message = "Utterance not found: " + *options.utteranceId;
            return result;
        }
    } else {
        const std::size_t limit = options.allMode ? 0 : options.subsetCount;
        utterances = listLibriSpeechUtterances(datasetRoot, limit);
    }

    const auto durationsText = readFileToString(options.durationsTsv, error);
    if (!durationsText) {
        result.exitCode = LibriSpeechPlaybackCliExitCode::RuntimeError;
        result.message = error;
        return result;
    }
    const auto durations = parseLibriSpeechDurationTsv(*durationsText, error);
    if (!error.empty()) {
        result.exitCode = LibriSpeechPlaybackCliExitCode::RuntimeError;
        result.message = error;
        return result;
    }

    LivePhonemeVerifyRunPaths runPaths;
    if (options.runDirectoryOverride) {
        runPaths.runDirectory = *options.runDirectoryOverride;
        runPaths.liveLog = runPaths.runDirectory / "live-log.jsonl";
        runPaths.manifest = runPaths.runDirectory / "manifest.json";
        std::error_code ec;
        std::filesystem::create_directories(runPaths.runDirectory, ec);
    } else if (!createLivePhonemeVerifyRun(options.verifyRoot, runPaths, error)) {
        result.exitCode = LibriSpeechPlaybackCliExitCode::RuntimeError;
        result.message = error;
        return result;
    }

    const auto built =
        buildLibriSpeechPlaybackPlan(utterances, durations, route, options.gapSeconds, runPaths.runDirectory);
    if (!built.ok) {
        result.exitCode = LibriSpeechPlaybackCliExitCode::RuntimeError;
        result.message = built.error;
        return result;
    }

    const auto manifestPath = options.manifestPathOverride
                                  ? *options.manifestPathOverride
                                  : defaultLibriSpeechPlaybackManifestPath(runPaths.runDirectory);
    if (!writeLibriSpeechPlaybackManifest(built.plan, manifestPath, error)) {
        result.exitCode = LibriSpeechPlaybackCliExitCode::RuntimeError;
        result.message = error;
        return result;
    }

    std::ostringstream message;
    message << "Wrote playback manifest: " << manifestPath.string() << '\n'
            << "run directory: " << runPaths.runDirectory.string() << '\n'
            << "clips: " << built.plan.clips.size() << '\n'
            << "gap seconds: " << built.plan.gapSeconds << '\n'
            << "total duration seconds: " << built.plan.totalDurationSeconds << '\n'
            << "playback device: " << built.plan.playbackDevice << '\n'
            << "route: " << built.plan.routeId;
    result.exitCode = LibriSpeechPlaybackCliExitCode::Success;
    result.message = message.str();
    return result;
}

} // namespace Voice2VocalSynth
