#include "Voice2VocalSynth/PhonemeEvalCli.h"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace Voice2VocalSynth
{
namespace
{

[[nodiscard]] bool startsWith(std::string_view value, std::string_view prefix)
{
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] std::optional<double> parsePositiveDouble(std::string_view text, std::string& error)
{
    try {
        std::size_t consumed = 0;
        const double value = std::stod(std::string(text), &consumed);
        if (consumed != text.size() || value <= 0.0) {
            error = "Expected a positive number: " + std::string(text);
            return std::nullopt;
        }
        return value;
    } catch (const std::exception&) {
        error = "Expected a positive number: " + std::string(text);
        return std::nullopt;
    }
}

} // namespace

std::string phonemeEvalCliUsage()
{
    return "Voice2VocalSynthPhonemeEval --reference <reference_frames.json> "
           "--prediction <predicted_frames.json> "
           "[--out <metrics.json>] "
           "[--max-onset-error-ms <ms>] "
           "[--min-overlap-ms <ms>]";
}

std::optional<PhonemeEvalCliOptions> parsePhonemeEvalCliArgs(
    const std::vector<std::string>& args,
    std::string& error)
{
    PhonemeEvalCliOptions options;
    bool haveReference = false;
    bool havePrediction = false;

    for (std::size_t index = 1; index < args.size(); ++index) {
        const auto& arg = args[index];
        if (arg == "--reference") {
            if (index + 1 >= args.size()) {
                error = "Missing value for --reference";
                return std::nullopt;
            }
            options.referencePath = args[++index];
            haveReference = true;
            continue;
        }
        if (arg == "--prediction") {
            if (index + 1 >= args.size()) {
                error = "Missing value for --prediction";
                return std::nullopt;
            }
            options.predictionPath = args[++index];
            havePrediction = true;
            continue;
        }
        if (arg == "--out") {
            if (index + 1 >= args.size()) {
                error = "Missing value for --out";
                return std::nullopt;
            }
            options.outputPath = args[++index];
            continue;
        }
        if (arg == "--max-onset-error-ms") {
            if (index + 1 >= args.size()) {
                error = "Missing value for --max-onset-error-ms";
                return std::nullopt;
            }
            const auto value = parsePositiveDouble(args[++index], error);
            if (!value) {
                return std::nullopt;
            }
            options.evalOptions.maxOnsetErrorSeconds = *value / 1000.0;
            continue;
        }
        if (arg == "--min-overlap-ms") {
            if (index + 1 >= args.size()) {
                error = "Missing value for --min-overlap-ms";
                return std::nullopt;
            }
            const auto value = parsePositiveDouble(args[++index], error);
            if (!value) {
                return std::nullopt;
            }
            options.evalOptions.minOverlapSeconds = *value / 1000.0;
            continue;
        }
        if (startsWith(arg, "-")) {
            error = "Unknown option: " + arg;
            return std::nullopt;
        }

        error = "Unexpected positional argument: " + arg;
        return std::nullopt;
    }

    if (!haveReference || !havePrediction) {
        error = "Both --reference and --prediction are required";
        return std::nullopt;
    }

    return options;
}

std::string formatPhonemeEvaluationSummary(const PhonemeEvaluationMetrics& metrics)
{
    std::ostringstream summary;
    summary << std::fixed << std::setprecision(3);
    summary << "reference=" << metrics.referenceCount
            << " prediction=" << metrics.predictionCount
            << " matched=" << metrics.matchedCount
            << " missed=" << metrics.missedCount
            << " false_positive=" << metrics.falsePositiveCount
            << " precision=" << metrics.precision
            << " recall=" << metrics.recall
            << " f1=" << metrics.f1
            << " mean_abs_onset_error_ms=" << metrics.meanAbsoluteOnsetErrorMs;
    return summary.str();
}

PhonemeEvalCliResult runPhonemeEvalCli(const PhonemeEvalCliOptions& options)
{
    PhonemeEvalCliResult result;

    const auto reference = loadPhonemeFrameLabelsJson(options.referencePath);
    if (!reference.ok) {
        result.exitCode = PhonemeEvalCliExitCode::RuntimeError;
        result.summary = reference.error;
        return result;
    }

    const auto prediction = loadPhonemeFrameLabelsJson(options.predictionPath);
    if (!prediction.ok) {
        result.exitCode = PhonemeEvalCliExitCode::RuntimeError;
        result.summary = prediction.error;
        return result;
    }

    const auto metrics = evaluatePhonemeFrames(reference.frames, prediction.frames, options.evalOptions);
    result.summary = formatPhonemeEvaluationSummary(metrics);
    result.metricsJson = phonemeEvaluationMetricsToJson(metrics);

    if (options.outputPath) {
        std::ofstream output(*options.outputPath, std::ios::binary);
        if (!output) {
            result.exitCode = PhonemeEvalCliExitCode::RuntimeError;
            result.summary = "Unable to write metrics JSON: " + options.outputPath->string();
            return result;
        }
        output << *result.metricsJson;
    }

    result.exitCode = PhonemeEvalCliExitCode::Success;
    return result;
}

} // namespace Voice2VocalSynth
