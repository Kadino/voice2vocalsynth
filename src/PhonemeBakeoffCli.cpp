#include "Voice2VocalSynth/PhonemeBakeoffCli.h"

#include "Voice2VocalSynth/PcmWavReader.h"
#include "Voice2VocalSynth/PhonemeBackend.h"

#include <cctype>
#include <fstream>
#include <memory>
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

[[nodiscard]] std::vector<std::string> splitCommaList(std::string_view text)
{
    std::vector<std::string> values;
    std::string current;
    for (const char character : text) {
        if (character == ',') {
            if (!current.empty()) {
                values.push_back(current);
                current.clear();
            }
            continue;
        }
        if (!std::isspace(static_cast<unsigned char>(character))) {
            current.push_back(character);
        }
    }
    if (!current.empty()) {
        values.push_back(current);
    }
    return values;
}

void appendBackend(std::vector<std::unique_ptr<IPhonemeBackend>>& backends,
                   const std::string& name,
                   const PhonemeBakeoffCliOptions& options,
                   std::string& error)
{
    if (name == "placeholder") {
        backends.push_back(std::make_unique<PlaceholderPitchPhonemeBackend>());
        return;
    }
    if (name == "onnx") {
        if (!options.onnxModelPath) {
            error = "Backend onnx requires --onnx-model";
            return;
        }
        PhonemeOnnxBackendOptions onnxOptions;
        onnxOptions.modelPath = *options.onnxModelPath;
        if (options.onnxConfigPath) {
            onnxOptions.configPath = *options.onnxConfigPath;
        }
        auto backend = std::make_unique<PhonemeOnnxBackend>(std::move(onnxOptions));
        std::string loadError;
        if (!backend->load(loadError)) {
            error = loadError;
            return;
        }
        backends.push_back(std::move(backend));
        return;
    }
    error = "Unknown backend: " + name;
}

} // namespace

std::string phonemeBakeoffCliUsage()
{
    return "Voice2VocalSynthPhonemeBakeoff "
           "--reference <labels.json> --audio <clip.wav> "
           "[--backends placeholder,onnx] "
           "[--eval-data <EvalDataRoot> --clip <basename>] "
           "[--onnx-model <model.onnx> --onnx-config <model.phoneme.json>] "
           "[--out <report.json>] "
           "[--max-onset-error-ms <ms>] [--min-overlap-ms <ms>]";
}

std::optional<PhonemeBakeoffCliOptions> parsePhonemeBakeoffCliArgs(
    const std::vector<std::string>& args,
    std::string& error)
{
    PhonemeBakeoffCliOptions options;
    options.backendNames = {"placeholder"};
    bool haveReference = false;
    bool haveAudio = false;

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
        if (arg == "--audio") {
            if (index + 1 >= args.size()) {
                error = "Missing value for --audio";
                return std::nullopt;
            }
            options.audioPath = args[++index];
            haveAudio = true;
            continue;
        }
        if (arg == "--eval-data") {
            if (index + 1 >= args.size()) {
                error = "Missing value for --eval-data";
                return std::nullopt;
            }
            options.evalDataRoot = args[++index];
            continue;
        }
        if (arg == "--clip") {
            if (index + 1 >= args.size()) {
                error = "Missing value for --clip";
                return std::nullopt;
            }
            options.clipName = args[++index];
            continue;
        }
        if (arg == "--backends") {
            if (index + 1 >= args.size()) {
                error = "Missing value for --backends";
                return std::nullopt;
            }
            options.backendNames = splitCommaList(args[++index]);
            if (options.backendNames.empty()) {
                error = "--backends must list at least one backend";
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--onnx-model") {
            if (index + 1 >= args.size()) {
                error = "Missing value for --onnx-model";
                return std::nullopt;
            }
            options.onnxModelPath = args[++index];
            continue;
        }
        if (arg == "--onnx-config") {
            if (index + 1 >= args.size()) {
                error = "Missing value for --onnx-config";
                return std::nullopt;
            }
            options.onnxConfigPath = args[++index];
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

    if (options.evalDataRoot && options.clipName) {
        const auto layout = evalDataLayout(*options.evalDataRoot);
        if (!haveReference) {
            options.referencePath = layout.labels / (*options.clipName + ".json");
            haveReference = true;
        }
        if (!haveAudio) {
            options.audioPath = layout.recordings / (*options.clipName + ".wav");
            haveAudio = true;
        }
    }

    if (!haveReference || !haveAudio) {
        error = "Both --reference and --audio are required unless --eval-data and --clip are provided";
        return std::nullopt;
    }

    return options;
}

PhonemeBakeoffCliResult runPhonemeBakeoffCli(const PhonemeBakeoffCliOptions& options)
{
    PhonemeBakeoffCliResult result;
    if (options.evalDataRoot) {
        std::string layoutError;
        if (!ensureEvalDataLayout(*options.evalDataRoot, layoutError)) {
            result.summary = layoutError;
            return result;
        }
    }

    const auto reference = loadPhonemeFrameLabelsJson(options.referencePath);
    if (!reference.ok) {
        result.summary = reference.error;
        return result;
    }

    const auto audio = PcmWavReader::loadMonoFloat(options.audioPath);
    if (!audio.ok) {
        result.summary = audio.error;
        return result;
    }

    std::vector<std::unique_ptr<IPhonemeBackend>> ownedBackends;
    std::vector<IPhonemeBackend*> backendPtrs;
    std::string backendError;
    for (const auto& backendName : options.backendNames) {
        appendBackend(ownedBackends, backendName, options, backendError);
        if (!backendError.empty()) {
            result.summary = backendError;
            return result;
        }
        backendPtrs.push_back(ownedBackends.back().get());
    }

    const auto report = runPhonemeBakeoff(reference.frames,
                                          backendPtrs,
                                          audio.mono,
                                          static_cast<double>(audio.sampleRate),
                                          options.evalOptions);
    result.reportJson = phonemeBakeoffReportToJson(report);

    std::ostringstream summary;
    for (const auto& entry : report.entries) {
        summary << entry.backendName << " raw_f1=" << entry.rawMetrics.f1
                << " stabilized_f1=" << entry.stabilizedMetrics.f1
                << " p95_onset_ms=" << entry.stabilizedMetrics.p95OnsetErrorMs
                << " p95_latency_ms=" << entry.p95BackendLatencyMs << '\n';
    }
    result.summary = summary.str();
    if (result.summary.empty()) {
        result.summary = "No backends evaluated";
    }

    if (options.outputPath) {
        std::ofstream output(*options.outputPath, std::ios::binary);
        if (!output) {
            result.summary = "Unable to write bakeoff report: " + options.outputPath->string();
            return result;
        }
        output << *result.reportJson;
    }

    result.exitCode = PhonemeBakeoffCliExitCode::Success;
    return result;
}

} // namespace Voice2VocalSynth
