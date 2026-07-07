#include "Voice2VocalSynth/PhonemeBakeoffCli.h"

#include "Voice2VocalSynth/PcmWavReader.h"
#include "Voice2VocalSynth/PhonemeBackend.h"

#include <cctype>
#include <fstream>
#include <memory>
#include <optional>
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

[[nodiscard]] std::optional<PhonemeBakeoffReport> runBakeoffReportForClip(
    const PhonemeBakeoffCliOptions& options,
    const std::filesystem::path& referencePath,
    const std::filesystem::path& audioPath,
    std::string& error)
{
    const auto reference = loadPhonemeFrameLabelsJson(referencePath);
    if (!reference.ok) {
        error = reference.error;
        return std::nullopt;
    }

    const auto audio = PcmWavReader::loadMonoFloat(audioPath);
    if (!audio.ok) {
        error = audio.error;
        return std::nullopt;
    }

    std::vector<std::unique_ptr<IPhonemeBackend>> ownedBackends;
    std::vector<IPhonemeBackend*> backendPtrs;
    for (const auto& backendName : options.backendNames) {
        appendBackend(ownedBackends, backendName, options, error);
        if (!error.empty()) {
            return std::nullopt;
        }
        backendPtrs.push_back(ownedBackends.back().get());
    }

    return runPhonemeBakeoff(reference.frames,
                             backendPtrs,
                             audio.mono,
                             static_cast<double>(audio.sampleRate),
                             options.evalOptions);
}

[[nodiscard]] std::string phonemeBakeoffBatchReportToJson(
    const std::vector<std::pair<std::string, PhonemeBakeoffReport>>& clipReports)
{
    std::ostringstream json;
    json << std::fixed;
    json.precision(6);
    json << "{\n  \"schemaVersion\": 1,\n  \"clips\": [\n";
    for (std::size_t clipIndex = 0; clipIndex < clipReports.size(); ++clipIndex) {
        const auto& [clipName, report] = clipReports[clipIndex];
        json << "    {\n";
        json << "      \"clip\": \"" << clipName << "\",\n";
        json << "      \"entries\": [\n";
        for (std::size_t index = 0; index < report.entries.size(); ++index) {
            const auto& entry = report.entries[index];
            json << "        {\n";
            json << "          \"backendName\": \"" << entry.backendName << "\",\n";
            json << "          \"meanBackendLatencyMs\": " << entry.meanBackendLatencyMs << ",\n";
            json << "          \"p95BackendLatencyMs\": " << entry.p95BackendLatencyMs << ",\n";
            json << "          \"stabilizedF1\": " << entry.stabilizedMetrics.f1 << ",\n";
            json << "          \"stabilizedP95OnsetErrorMs\": " << entry.stabilizedMetrics.p95OnsetErrorMs
                 << "\n";
            json << "        }";
            if (index + 1 < report.entries.size()) {
                json << ',';
            }
            json << '\n';
        }
        json << "      ]\n";
        json << "    }";
        if (clipIndex + 1 < clipReports.size()) {
            json << ',';
        }
        json << '\n';
    }
    json << "  ]\n}\n";
    return json.str();
}

PhonemeBakeoffCliResult runSingleClipBakeoff(const PhonemeBakeoffCliOptions& options,
                                             const std::filesystem::path& referencePath,
                                             const std::filesystem::path& audioPath)
{
    PhonemeBakeoffCliResult result;
    std::string error;
    const auto report = runBakeoffReportForClip(options, referencePath, audioPath, error);
    if (!report) {
        result.summary = error;
        return result;
    }

    result.reportJson = phonemeBakeoffReportToJson(*report);
    std::ostringstream summary;
    for (const auto& entry : report->entries) {
        summary << entry.backendName << " raw_f1=" << entry.rawMetrics.f1
                << " stabilized_f1=" << entry.stabilizedMetrics.f1
                << " p95_onset_ms=" << entry.stabilizedMetrics.p95OnsetErrorMs
                << " p95_latency_ms=" << entry.p95BackendLatencyMs << '\n';
    }
    result.summary = summary.str();
    if (result.summary.empty()) {
        result.summary = "No backends evaluated";
    }
    result.exitCode = PhonemeBakeoffCliExitCode::Success;
    return result;
}

} // namespace

std::string phonemeBakeoffCliUsage()
{
    return "Voice2VocalSynthPhonemeBakeoff "
           "--reference <labels.json> --audio <clip.wav> "
           "[--backends placeholder,onnx] "
           "[--eval-data <EvalDataRoot> --clip <basename> | --all-clips] "
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
        if (arg == "--all-clips") {
            options.allClips = true;
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

    if (options.allClips) {
        if (!options.evalDataRoot) {
            error = "--all-clips requires --eval-data";
            return std::nullopt;
        }
        return options;
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
        error = "Provide --reference and --audio, or --eval-data with --clip, or --eval-data with --all-clips";
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

    if (options.allClips) {
        if (!options.evalDataRoot) {
            result.summary = "--all-clips requires --eval-data";
            return result;
        }

        const auto clips = listEvalClipNames(*options.evalDataRoot);
        if (clips.empty()) {
            result.summary = "No eval clips with matching recordings/ and labels/ pairs were found";
            return result;
        }

        const auto layout = evalDataLayout(*options.evalDataRoot);
        std::vector<std::pair<std::string, PhonemeBakeoffReport>> clipReports;
        std::ostringstream summary;
        for (const auto& clip : clips) {
            const auto referencePath = layout.labels / (clip + ".json");
            const auto audioPath = layout.recordings / (clip + ".wav");
            std::string clipError;
            const auto report = runBakeoffReportForClip(options, referencePath, audioPath, clipError);
            if (!report) {
                result.summary = "Clip " + clip + " failed: " + clipError;
                return result;
            }

            clipReports.emplace_back(clip, *report);
            summary << clip << ": ";
            for (const auto& entry : report->entries) {
                summary << entry.backendName << " f1=" << entry.stabilizedMetrics.f1 << ' ';
            }
            summary << '\n';
        }

        result.reportJson = phonemeBakeoffBatchReportToJson(clipReports);
        result.summary = summary.str();
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

    auto singleResult = runSingleClipBakeoff(options, options.referencePath, options.audioPath);
    if (singleResult.exitCode != PhonemeBakeoffCliExitCode::Success) {
        return singleResult;
    }

    if (options.outputPath && singleResult.reportJson) {
        std::ofstream output(*options.outputPath, std::ios::binary);
        if (!output) {
            singleResult.summary = "Unable to write bakeoff report: " + options.outputPath->string();
            singleResult.exitCode = PhonemeBakeoffCliExitCode::RuntimeError;
            return singleResult;
        }
        output << *singleResult.reportJson;
    }

    return singleResult;
}

} // namespace Voice2VocalSynth
