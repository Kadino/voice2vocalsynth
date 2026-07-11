#include "Voice2VocalSynth/LivePhonemeVerifyCli.h"

#include "Voice2VocalSynth/LibriSpeechPlayback.h"
#include "Voice2VocalSynth/LivePhonemeVerification.h"
#include "Voice2VocalSynth/LivePhonemeVerifyPaths.h"

#include <filesystem>
#include <cmath>
#include <optional>

namespace Voice2VocalSynth
{
namespace
{

struct Options
{
    std::filesystem::path liveLog;
    std::filesystem::path playbackManifest;
    std::filesystem::path labelsRoot;
    std::filesystem::path predictionsOut;
    std::filesystem::path metricsOut;
    std::filesystem::path reportOut;
    std::string backend;
    LiveVerificationGateOptions gates;
};

std::optional<double> parseDouble(const std::string& value)
{
    try {
        std::size_t consumed = 0;
        const double parsed = std::stod(value, &consumed);
        if (consumed != value.size() || !std::isfinite(parsed)) {
            return std::nullopt;
        }
        return parsed;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<Options> parseOptions(const std::vector<std::string>& args, std::string& error)
{
    Options options;
    for (std::size_t index = 1; index < args.size(); ++index) {
        const auto& argument = args[index];
        if (argument == "--help" || argument == "-h") {
            error.clear();
            return std::nullopt;
        }
        if (index + 1 >= args.size()) {
            error = "Missing value for " + argument;
            return std::nullopt;
        }
        const auto& value = args[++index];
        if (argument == "--live-log") {
            options.liveLog = value;
        } else if (argument == "--playback-manifest") {
            options.playbackManifest = value;
        } else if (argument == "--labels-root") {
            options.labelsRoot = value;
        } else if (argument == "--predictions-out") {
            options.predictionsOut = value;
        } else if (argument == "--metrics-out") {
            options.metricsOut = value;
        } else if (argument == "--report-out") {
            options.reportOut = value;
        } else if (argument == "--backend") {
            options.backend = value;
        } else {
            const auto number = parseDouble(value);
            if (!number) {
                error = "Invalid numeric value for " + argument;
                return std::nullopt;
            }
            if (argument == "--max-e2e-latency-ms") {
                options.gates.maxEndToEndLatencyMs = *number;
            } else if (argument == "--min-f1") {
                options.gates.minF1 = *number;
            } else if (argument == "--max-mean-onset-error-ms") {
                options.gates.maxMeanOnsetErrorMs = *number;
            } else if (argument == "--max-p95-onset-error-ms") {
                options.gates.maxP95OnsetErrorMs = *number;
            } else if (argument == "--max-mean-end-error-ms") {
                options.gates.maxMeanEndErrorMs = *number;
            } else if (argument == "--max-p95-end-error-ms") {
                options.gates.maxP95EndErrorMs = *number;
            } else if (argument == "--max-mean-duration-error-ms") {
                options.gates.maxMeanDurationErrorMs = *number;
            } else if (argument == "--max-missed-consonant-rate") {
                options.gates.maxMissedConsonantRate = *number;
            } else {
                error = "Unknown argument: " + argument;
                return std::nullopt;
            }
        }
    }
    if (options.liveLog.empty() || options.playbackManifest.empty() ||
        options.labelsRoot.empty()) {
        error = "--live-log, --playback-manifest, and --labels-root are required";
        return std::nullopt;
    }
    const auto nonNegative = [](const std::optional<double>& value) {
        return !value || *value >= 0.0;
    };
    if (options.gates.maxEndToEndLatencyMs <= 0.0 ||
        !nonNegative(options.gates.maxMeanOnsetErrorMs) ||
        !nonNegative(options.gates.maxP95OnsetErrorMs) ||
        !nonNegative(options.gates.maxMeanEndErrorMs) ||
        !nonNegative(options.gates.maxP95EndErrorMs) ||
        !nonNegative(options.gates.maxMeanDurationErrorMs) ||
        (options.gates.minF1 &&
         (*options.gates.minF1 < 0.0 || *options.gates.minF1 > 1.0)) ||
        (options.gates.maxMissedConsonantRate &&
         (*options.gates.maxMissedConsonantRate < 0.0 ||
          *options.gates.maxMissedConsonantRate > 1.0))) {
        error = "Latency/error limits must be non-negative and F1/rates must be within [0,1]";
        return std::nullopt;
    }
    const auto defaults = livePhonemeVerifyRunPaths(options.liveLog.parent_path());
    if (options.predictionsOut.empty()) {
        options.predictionsOut = defaults.predictions;
    }
    if (options.metricsOut.empty()) {
        options.metricsOut = defaults.metrics;
    }
    if (options.reportOut.empty()) {
        options.reportOut = defaults.report;
    }
    return options;
}

} // namespace

std::string livePhonemeVerifyCliUsage()
{
    return
        "Voice2VocalSynthLivePhonemeVerify --live-log <live-log.jsonl> "
        "--playback-manifest <playback-manifest.json> --labels-root <directory> "
        "[--backend <name>] [--predictions-out <path>] [--metrics-out <path>] "
        "[--report-out <path>] [--max-e2e-latency-ms <ms>] "
        "--min-f1 <0..1> --max-mean-onset-error-ms <ms> "
        "--max-p95-onset-error-ms <ms> --max-mean-end-error-ms <ms> "
        "--max-p95-end-error-ms <ms> --max-mean-duration-error-ms <ms> "
        "--max-missed-consonant-rate <0..1>";
}

LivePhonemeVerifyCliResult runLivePhonemeVerifyCli(const std::vector<std::string>& args)
{
    LivePhonemeVerifyCliResult result;
    std::string error;
    const auto options = parseOptions(args, error);
    if (!options) {
        result.exitCode = LivePhonemeVerifyCliExitCode::Usage;
        result.message = error.empty() ? livePhonemeVerifyCliUsage()
                                       : error + "\n" + livePhonemeVerifyCliUsage();
        return result;
    }

    const auto liveLog = loadLivePhonemeLogJsonl(options->liveLog);
    if (!liveLog.ok) {
        result.exitCode = LivePhonemeVerifyCliExitCode::RuntimeError;
        result.message = liveLog.error;
        return result;
    }
    const auto playback = loadLibriSpeechPlaybackManifest(options->playbackManifest);
    if (!playback.ok) {
        result.exitCode = LivePhonemeVerifyCliExitCode::RuntimeError;
        result.message = playback.error;
        return result;
    }
    const std::string backend =
        options->backend.empty() ? liveLog.log.sessionBackend : options->backend;
    const auto verified =
        verifyLivePhonemeRun(liveLog.log, playback.plan, options->labelsRoot, backend, options->gates);
    if (!verified.ok) {
        result.exitCode = LivePhonemeVerifyCliExitCode::RuntimeError;
        result.message = verified.error;
        return result;
    }
    if (!writeLivePhonemeVerificationOutputs(verified.report,
                                             options->predictionsOut,
                                             options->metricsOut,
                                             options->reportOut,
                                             error)) {
        result.exitCode = LivePhonemeVerifyCliExitCode::RuntimeError;
        result.message = error;
        return result;
    }

    result.exitCode = verified.report.gates.passed
                          ? LivePhonemeVerifyCliExitCode::Success
                          : LivePhonemeVerifyCliExitCode::GateFailed;
    result.message = std::string(verified.report.gates.passed ? "Live verification passed"
                                                              : "Live verification gates failed") +
                     "\nMetrics: " + options->metricsOut.string() +
                     "\nReport: " + options->reportOut.string();
    return result;
}

} // namespace Voice2VocalSynth
