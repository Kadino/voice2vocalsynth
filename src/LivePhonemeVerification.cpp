#include "Voice2VocalSynth/LivePhonemeVerification.h"

#include "Voice2VocalSynth/LatencyBudget.h"
#include "Voice2VocalSynth/MfaLabelPipeline.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace Voice2VocalSynth
{
namespace
{

std::size_t valuePosition(std::string_view json, std::string_view key)
{
    const auto keyPosition = json.find("\"" + std::string(key) + "\"");
    if (keyPosition == std::string_view::npos) {
        return std::string_view::npos;
    }
    auto position = json.find(':', keyPosition + key.size() + 2);
    if (position == std::string_view::npos) {
        return position;
    }
    for (++position; position < json.size() &&
                     std::isspace(static_cast<unsigned char>(json[position])); ++position) {
    }
    return position;
}

std::optional<std::string> stringField(std::string_view json, std::string_view key)
{
    auto position = valuePosition(json, key);
    if (position == std::string_view::npos || position >= json.size() || json[position] != '"') {
        return std::nullopt;
    }
    std::string value;
    bool escaped = false;
    for (++position; position < json.size(); ++position) {
        const char character = json[position];
        if (escaped) {
            switch (character) {
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            default: value.push_back(character); break;
            }
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else if (character == '"') {
            return value;
        } else {
            value.push_back(character);
        }
    }
    return std::nullopt;
}

std::optional<double> numberField(std::string_view json, std::string_view key)
{
    const auto position = valuePosition(json, key);
    if (position == std::string_view::npos) {
        return std::nullopt;
    }
    std::size_t end = position;
    while (end < json.size() &&
           (std::isdigit(static_cast<unsigned char>(json[end])) || json[end] == '-' ||
            json[end] == '+' || json[end] == '.' || json[end] == 'e' || json[end] == 'E')) {
        ++end;
    }
    try {
        return std::stod(std::string(json.substr(position, end - position)));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<bool> boolField(std::string_view json, std::string_view key)
{
    const auto position = valuePosition(json, key);
    if (position == std::string_view::npos) {
        return std::nullopt;
    }
    if (json.substr(position, 4) == "true") {
        return true;
    }
    if (json.substr(position, 5) == "false") {
        return false;
    }
    return std::nullopt;
}

std::string jsonEscape(std::string_view value)
{
    std::string escaped;
    for (const char character : value) {
        switch (character) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped.push_back(character); break;
        }
    }
    return escaped;
}

std::string canonicalBackend(std::string_view backend)
{
    if (backend == "pocketsphinx") {
        return "pocketsphinx_allphone";
    }
    if (backend == "onnx_phoneme" || backend == "onnx") {
        return "phoneme_onnx";
    }
    if (backend == "placeholder") {
        return "placeholder_pitch_gate";
    }
    return std::string(backend);
}

LiveLatencyDistribution distribution(std::vector<double> samplesMs)
{
    LiveLatencyDistribution result;
    samplesMs.erase(std::remove_if(samplesMs.begin(),
                                   samplesMs.end(),
                                   [](double value) {
                                       return !std::isfinite(value) || value < 0.0;
                                   }),
                    samplesMs.end());
    result.sampleCount = samplesMs.size();
    if (samplesMs.empty()) {
        return result;
    }
    std::vector<double> samplesSeconds;
    samplesSeconds.reserve(samplesMs.size());
    for (const double sample : samplesMs) {
        samplesSeconds.push_back(sample / 1000.0);
    }
    result.p50Ms = percentileSeconds(samplesSeconds, 0.50) * 1000.0;
    result.p95Ms = percentileSeconds(samplesSeconds, 0.95) * 1000.0;
    result.p99Ms = percentileSeconds(samplesSeconds, 0.99) * 1000.0;
    result.maxMs = *std::max_element(samplesMs.begin(), samplesMs.end());
    return result;
}

void shiftFrames(std::vector<PhonemeFrame>& frames, const double offset)
{
    for (auto& frame : frames) {
        frame.estimatedOnsetSeconds += offset;
        frame.estimatedEndSeconds += offset;
    }
}

void appendQualityJson(std::ostringstream& json,
                       const PhonemeEvaluationMetrics& metrics,
                       std::string_view indent)
{
    const std::string i(indent);
    json << i << "\"referenceCount\": " << metrics.referenceCount << ",\n";
    json << i << "\"predictionCount\": " << metrics.predictionCount << ",\n";
    json << i << "\"matchedCount\": " << metrics.matchedCount << ",\n";
    json << i << "\"missedCount\": " << metrics.missedCount << ",\n";
    json << i << "\"falsePositiveCount\": " << metrics.falsePositiveCount << ",\n";
    json << i << "\"missedConsonantCount\": " << metrics.missedConsonantCount << ",\n";
    json << i << "\"precision\": " << metrics.precision << ",\n";
    json << i << "\"recall\": " << metrics.recall << ",\n";
    json << i << "\"f1\": " << metrics.f1 << ",\n";
    json << i << "\"falsePositiveRate\": " << metrics.falsePositiveRate << ",\n";
    json << i << "\"missedRate\": " << metrics.missedRate << ",\n";
    json << i << "\"missedConsonantRate\": " << metrics.missedConsonantRate << ",\n";
    json << i << "\"meanAbsoluteOnsetErrorMs\": " << metrics.meanAbsoluteOnsetErrorMs << ",\n";
    json << i << "\"p95OnsetErrorMs\": " << metrics.p95OnsetErrorMs << ",\n";
    json << i << "\"meanAbsoluteEndErrorMs\": " << metrics.meanAbsoluteEndErrorMs << ",\n";
    json << i << "\"p95EndErrorMs\": " << metrics.p95EndErrorMs << ",\n";
    json << i << "\"meanAbsoluteDurationErrorMs\": " << metrics.meanAbsoluteDurationErrorMs << ",\n";
    json << i << "\"p95DurationErrorMs\": " << metrics.p95DurationErrorMs << ",\n";
    json << i << "\"confusionCounts\": {";
    std::size_t index = 0;
    for (const auto& [pair, count] : metrics.confusionCounts) {
        json << (index++ == 0 ? "\n" : ",\n") << i << "  \"" << jsonEscape(pair)
             << "\": " << count;
    }
    if (!metrics.confusionCounts.empty()) {
        json << '\n' << i;
    }
    json << "}";
}

void appendDistributionJson(std::ostringstream& json,
                            const LiveLatencyDistribution& value,
                            std::string_view indent)
{
    json << "{\n";
    json << indent << "  \"sampleCount\": " << value.sampleCount << ",\n";
    json << indent << "  \"p50Ms\": " << value.p50Ms << ",\n";
    json << indent << "  \"p95Ms\": " << value.p95Ms << ",\n";
    json << indent << "  \"p99Ms\": " << value.p99Ms << ",\n";
    json << indent << "  \"maxMs\": " << value.maxMs << "\n";
    json << indent << "}";
}

bool writeTextFile(const std::filesystem::path& path,
                   std::string_view contents,
                   std::string& error)
{
    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Unable to write verification output: " + path.string();
        return false;
    }
    output << contents;
    if (!output) {
        error = "Failed while writing verification output: " + path.string();
        return false;
    }
    return true;
}

} // namespace

LivePhonemeLogLoadResult parseLivePhonemeLogJsonl(std::string_view jsonl)
{
    LivePhonemeLogLoadResult result;
    std::istringstream input{std::string(jsonl)};
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (line.empty()) {
            continue;
        }
        const auto kind = stringField(line, "kind");
        if (!kind) {
            result.error = "Live JSONL line " + std::to_string(lineNumber) + " is missing kind";
            return result;
        }
        if (*kind == "session_start") {
            result.log.sessionBackend = stringField(line, "backend").value_or("");
            result.log.sessionSteadyNs =
                static_cast<std::int64_t>(numberField(line, "steady_ns").value_or(0.0));
            result.log.sessionStreamTimeSeconds =
                numberField(line, "stream_time_seconds").value_or(0.0);
        } else if (*kind == "ph_frame") {
            LivePhonemeFrameRecord record;
            record.backend = stringField(line, "backend").value_or("");
            record.frame.arpabet = stringField(line, "arpabet").value_or("");
            record.frame.confidence =
                static_cast<float>(numberField(line, "conf").value_or(0.0));
            record.frame.estimatedOnsetSeconds = numberField(line, "t0").value_or(0.0);
            record.frame.estimatedEndSeconds =
                numberField(line, "t1").value_or(record.frame.estimatedOnsetSeconds);
            record.frame.isVowel = boolField(line, "vowel").value_or(false);
            record.frame.isConsonant = boolField(line, "consonant").value_or(false);
            record.steadyNs =
                static_cast<std::int64_t>(numberField(line, "steady_ns").value_or(0.0));
            if (record.backend.empty() || record.frame.arpabet.empty() ||
                record.frame.estimatedEndSeconds < record.frame.estimatedOnsetSeconds) {
                result.error = "Live JSONL line " + std::to_string(lineNumber) +
                               " contains an invalid ph_frame";
                return result;
            }
            result.log.phonemeFrames.push_back(std::move(record));
        } else if (*kind == "backend_inference" || *kind == "onnx") {
            LiveBackendLatencyRecord record;
            record.backend = stringField(line, "backend").value_or(
                *kind == "onnx" ? "phoneme_onnx_async" : "");
            record.streamTimeSeconds =
                numberField(line, "t_stream").value_or(0.0);
            record.steadyNs =
                static_cast<std::int64_t>(numberField(line, "steady_ns").value_or(0.0));
            record.lagMs = numberField(line, "lag_ms").value_or(0.0);
            record.ok = boolField(line, "ok").value_or(false);
            if (record.ok) {
                result.log.backendLatencies.push_back(std::move(record));
            }
        } else if (*kind == "device_settings") {
            LiveDeviceSettingsRecord record;
            record.sampleRateHz = numberField(line, "sample_rate_hz").value_or(0.0);
            record.bufferSamples =
                static_cast<int>(numberField(line, "buffer_samples").value_or(0.0));
            record.inputLatencySamples =
                static_cast<int>(numberField(line, "input_latency_samples").value_or(0.0));
            record.outputLatencySamples =
                static_cast<int>(numberField(line, "output_latency_samples").value_or(0.0));
            record.inputBufferSamples =
                static_cast<int>(numberField(line, "input_buffer_samples").value_or(0.0));
            record.outputBufferSamples =
                static_cast<int>(numberField(line, "output_buffer_samples").value_or(0.0));
            result.log.deviceSettings = record;
        } else if (*kind == "latency_measure") {
            result.log.hasLatencyMeasurement = true;
            result.log.latencyMeasurementValid = boolField(line, "valid").value_or(false);
            result.log.measuredRoundTripMs =
                numberField(line, "round_trip_ms").value_or(0.0);
            result.log.estimatedEndToEndMs =
                numberField(line, "estimate_ms").value_or(0.0);
        }
    }
    if (result.log.sessionBackend.empty()) {
        result.error = "Live log does not contain a session_start backend";
        return result;
    }
    result.ok = true;
    return result;
}

LivePhonemeLogLoadResult loadLivePhonemeLogJsonl(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        LivePhonemeLogLoadResult result;
        result.error = "Unable to open live JSONL log: " + path.string();
        return result;
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    return parseLivePhonemeLogJsonl(contents.str());
}

std::vector<PhonemeFrame> convertLivePhonemeFrames(const LivePhonemeLog& log,
                                                  std::string_view backend)
{
    const auto canonical = canonicalBackend(backend);
    std::vector<PhonemeFrame> frames;
    for (const auto& record : log.phonemeFrames) {
        if (record.backend == canonical) {
            frames.push_back(record.frame);
        }
    }
    std::sort(frames.begin(), frames.end(), [](const auto& left, const auto& right) {
        return left.estimatedOnsetSeconds < right.estimatedOnsetSeconds;
    });
    return frames;
}

LivePhonemeVerificationResult verifyLivePhonemeRun(
    const LivePhonemeLog& log,
    const LibriSpeechPlaybackPlan& playback,
    const std::filesystem::path& labelsRoot,
    std::string_view backend,
    const LiveVerificationGateOptions& gates)
{
    LivePhonemeVerificationResult result;
    auto& report = result.report;
    report.backend = canonicalBackend(backend);
    report.predictions = convertLivePhonemeFrames(log, report.backend);
    report.predictionCount = report.predictions.size();

    if (playback.playbackStartedSteadyNs <= 0 || log.sessionSteadyNs <= 0) {
        result.error = "Playback/session monotonic anchors are required for live-log alignment";
        return result;
    }
    const double playbackStartCaptureSeconds =
        log.sessionStreamTimeSeconds +
        static_cast<double>(playback.playbackStartedSteadyNs - log.sessionSteadyNs) / 1.0e9;

    std::vector<PhonemeFrame> globalReference;
    std::vector<PhonemeFrame> globalPrediction;
    for (const auto& clip : playback.clips) {
        const auto loaded = loadPhonemeFrameLabelsJson(labelsRoot / (clip.utteranceId + ".json"));
        if (!loaded.ok) {
            result.error = loaded.error;
            return result;
        }
        const double clipStart = playbackStartCaptureSeconds + clip.startOffsetSeconds;
        const double clipEnd = clipStart + clip.durationSeconds;
        std::vector<PhonemeFrame> localPrediction;
        for (const auto& frame : report.predictions) {
            if (frame.estimatedEndSeconds < clipStart || frame.estimatedOnsetSeconds > clipEnd) {
                continue;
            }
            auto local = frame;
            local.estimatedOnsetSeconds =
                std::clamp(local.estimatedOnsetSeconds - clipStart, 0.0, clip.durationSeconds);
            local.estimatedEndSeconds =
                std::clamp(local.estimatedEndSeconds - clipStart, 0.0, clip.durationSeconds);
            if (local.estimatedEndSeconds >= local.estimatedOnsetSeconds) {
                localPrediction.push_back(local);
            }
        }

        PhonemeEvaluationOptions evaluationOptions;
        evaluationOptions.maxOnsetErrorSeconds = 0.50;
        evaluationOptions.minOverlapSeconds = 0.005;
        report.utterances.push_back(
            {clip.utteranceId,
             evaluatePhonemeFrames(loaded.frames, localPrediction, evaluationOptions)});

        auto shiftedReference = loaded.frames;
        shiftFrames(shiftedReference, clipStart);
        globalReference.insert(globalReference.end(),
                               shiftedReference.begin(),
                               shiftedReference.end());
        auto shiftedPrediction = localPrediction;
        shiftFrames(shiftedPrediction, clipStart);
        globalPrediction.insert(globalPrediction.end(),
                                shiftedPrediction.begin(),
                                shiftedPrediction.end());
    }

    PhonemeEvaluationOptions evaluationOptions;
    evaluationOptions.maxOnsetErrorSeconds = 0.50;
    evaluationOptions.minOverlapSeconds = 0.005;
    report.quality =
        evaluatePhonemeFrames(globalReference, globalPrediction, evaluationOptions);

    std::vector<double> backendLagMs;
    for (const auto& latency : log.backendLatencies) {
        if (latency.backend == report.backend) {
            backendLagMs.push_back(latency.lagMs);
        }
    }
    report.latency.backend = distribution(std::move(backendLagMs));

    std::vector<double> decisionLagMs;
    if (log.sessionSteadyNs > 0) {
        for (const auto& frame : log.phonemeFrames) {
            if (frame.backend != report.backend || frame.steadyNs <= 0) {
                continue;
            }
            const double emittedAfterAnchor =
                static_cast<double>(frame.steadyNs - log.sessionSteadyNs) / 1.0e9;
            const double streamAfterAnchor =
                frame.frame.estimatedEndSeconds - log.sessionStreamTimeSeconds;
            decisionLagMs.push_back((emittedAfterAnchor - streamAfterAnchor) * 1000.0);
        }
    }
    report.latency.decision = distribution(std::move(decisionLagMs));

    if (log.hasLatencyMeasurement && log.latencyMeasurementValid &&
        log.measuredRoundTripMs > 0.0) {
        report.latency.endToEndMs = log.measuredRoundTripMs;
        report.latency.endToEndSource = "loopback_measurement";
    } else if (log.deviceSettings && log.deviceSettings->sampleRateHz > 0.0) {
        AudioDeviceLatency device;
        device.sampleRateHz = log.deviceSettings->sampleRateHz;
        device.inputBufferSizeSamples = log.deviceSettings->inputBufferSamples;
        device.outputBufferSizeSamples = log.deviceSettings->outputBufferSamples;
        device.inputDeviceLatencySamples = log.deviceSettings->inputLatencySamples;
        device.outputDeviceLatencySamples = log.deviceSettings->outputLatencySamples;
        auto analysis = LatencyBudgetCalculator::presetSettings(LatencyPreset::Balanced);
        const auto estimate = LatencyBudgetCalculator::calculate(device, analysis);
        report.latency.endToEndMs =
            estimate.endToEndMonitoringLatencyMs() + report.latency.backend.p95Ms;
        report.latency.endToEndSource = "device_and_pipeline_estimate";
    } else if (log.estimatedEndToEndMs > 0.0) {
        report.latency.endToEndMs =
            log.estimatedEndToEndMs + report.latency.backend.p95Ms;
        report.latency.endToEndSource = "logged_estimate";
    } else {
        result.error = "Live log has no measured or estimable end-to-end latency";
        return result;
    }

    auto& failures = report.gates.failures;
    if (gates.rejectPlaceholderBackend &&
        report.backend.find("placeholder") != std::string::npos) {
        failures.push_back("placeholder/debug backends cannot pass live verification");
    }
    if (report.predictions.empty()) {
        failures.push_back("no ph_frame predictions were emitted by the selected backend");
    }
    if (report.latency.endToEndMs > gates.maxEndToEndLatencyMs) {
        failures.push_back("end-to-end latency exceeds " +
                           std::to_string(gates.maxEndToEndLatencyMs) + " ms");
    }
    const bool temporalConfigured =
        gates.minF1 && gates.maxMeanOnsetErrorMs && gates.maxP95OnsetErrorMs &&
        gates.maxMeanEndErrorMs && gates.maxP95EndErrorMs &&
        gates.maxMeanDurationErrorMs && gates.maxMissedConsonantRate;
    if (!temporalConfigured) {
        failures.push_back("all temporal correctness gates must be configured");
    } else {
        if (report.quality.f1 < *gates.minF1) {
            failures.push_back("F1 is below the configured minimum");
        }
        if (report.quality.meanAbsoluteOnsetErrorMs > *gates.maxMeanOnsetErrorMs) {
            failures.push_back("mean onset error exceeds the configured maximum");
        }
        if (report.quality.p95OnsetErrorMs > *gates.maxP95OnsetErrorMs) {
            failures.push_back("P95 onset error exceeds the configured maximum");
        }
        if (report.quality.meanAbsoluteEndErrorMs > *gates.maxMeanEndErrorMs) {
            failures.push_back("mean end error exceeds the configured maximum");
        }
        if (report.quality.p95EndErrorMs > *gates.maxP95EndErrorMs) {
            failures.push_back("P95 end error exceeds the configured maximum");
        }
        if (report.quality.meanAbsoluteDurationErrorMs > *gates.maxMeanDurationErrorMs) {
            failures.push_back("mean duration error exceeds the configured maximum");
        }
        if (report.quality.missedConsonantRate > *gates.maxMissedConsonantRate) {
            failures.push_back("missed consonant rate exceeds the configured maximum");
        }
    }
    report.gates.passed = failures.empty();
    result.ok = true;
    return result;
}

std::string livePhonemeVerificationMetricsJson(
    const LivePhonemeVerificationReport& report)
{
    std::ostringstream json;
    json << std::fixed << std::setprecision(6);
    json << "{\n";
    json << "  \"schemaVersion\": 1,\n";
    json << "  \"backend\": \"" << jsonEscape(report.backend) << "\",\n";
    json << "  \"quality\": {\n";
    appendQualityJson(json, report.quality, "    ");
    json << "\n  },\n";
    json << "  \"latency\": {\n";
    json << "    \"endToEndMs\": " << report.latency.endToEndMs << ",\n";
    json << "    \"endToEndSource\": \"" << jsonEscape(report.latency.endToEndSource)
         << "\",\n";
    json << "    \"decision\": ";
    appendDistributionJson(json, report.latency.decision, "    ");
    json << ",\n    \"backend\": ";
    appendDistributionJson(json, report.latency.backend, "    ");
    json << "\n  },\n";
    json << "  \"gates\": {\n";
    json << "    \"passed\": " << (report.gates.passed ? "true" : "false") << ",\n";
    json << "    \"failures\": [";
    for (std::size_t index = 0; index < report.gates.failures.size(); ++index) {
        json << (index == 0 ? "" : ", ") << '"' << jsonEscape(report.gates.failures[index])
             << '"';
    }
    json << "]\n  },\n";
    json << "  \"utterances\": [\n";
    for (std::size_t index = 0; index < report.utterances.size(); ++index) {
        json << "    {\"utteranceId\":\"" << jsonEscape(report.utterances[index].utteranceId)
             << "\",\"quality\":{\n";
        appendQualityJson(json, report.utterances[index].quality, "      ");
        json << "\n    }}";
        if (index + 1 < report.utterances.size()) {
            json << ',';
        }
        json << '\n';
    }
    json << "  ]\n";
    json << "}\n";
    return json.str();
}

std::string livePhonemeVerificationMarkdown(
    const LivePhonemeVerificationReport& report)
{
    std::ostringstream markdown;
    markdown << std::fixed << std::setprecision(2);
    markdown << "# Live phoneme verification report\n\n";
    markdown << "- Backend: `" << report.backend << "`\n";
    markdown << "- Result: **" << (report.gates.passed ? "PASS" : "FAIL") << "**\n";
    markdown << "- Predictions: " << report.predictionCount << "\n\n";
    markdown << "## Quality\n\n";
    markdown << "| Metric | Value |\n|---|---:|\n";
    markdown << "| Precision | " << report.quality.precision << " |\n";
    markdown << "| Recall | " << report.quality.recall << " |\n";
    markdown << "| F1 | " << report.quality.f1 << " |\n";
    markdown << "| False-positive rate | " << report.quality.falsePositiveRate << " |\n";
    markdown << "| Missed rate | " << report.quality.missedRate << " |\n";
    markdown << "| Missed consonant rate | " << report.quality.missedConsonantRate << " |\n";
    markdown << "| Mean / P95 onset error | " << report.quality.meanAbsoluteOnsetErrorMs
             << " / " << report.quality.p95OnsetErrorMs << " ms |\n";
    markdown << "| Mean / P95 end error | " << report.quality.meanAbsoluteEndErrorMs
             << " / " << report.quality.p95EndErrorMs << " ms |\n";
    markdown << "| Mean / P95 duration error | " << report.quality.meanAbsoluteDurationErrorMs
             << " / " << report.quality.p95DurationErrorMs << " ms |\n\n";
    markdown << "## Latency\n\n";
    markdown << "| Metric | P50 | P95 | P99 | Max | Samples |\n|---|---:|---:|---:|---:|---:|\n";
    markdown << "| Phoneme decision | " << report.latency.decision.p50Ms << " | "
             << report.latency.decision.p95Ms << " | " << report.latency.decision.p99Ms
             << " | " << report.latency.decision.maxMs << " | "
             << report.latency.decision.sampleCount << " |\n";
    markdown << "| Backend inference | " << report.latency.backend.p50Ms << " | "
             << report.latency.backend.p95Ms << " | " << report.latency.backend.p99Ms
             << " | " << report.latency.backend.maxMs << " | "
             << report.latency.backend.sampleCount << " |\n\n";
    markdown << "End-to-end: **" << report.latency.endToEndMs << " ms** (`"
             << report.latency.endToEndSource << "`)\n\n";
    markdown << "## Common confusions\n\n";
    if (report.quality.confusionCounts.empty()) {
        markdown << "None recorded.\n\n";
    } else {
        markdown << "| Reference → prediction | Count |\n|---|---:|\n";
        std::vector<std::pair<std::string, std::size_t>> confusions(
            report.quality.confusionCounts.begin(), report.quality.confusionCounts.end());
        std::sort(confusions.begin(), confusions.end(), [](const auto& left, const auto& right) {
            return left.second > right.second;
        });
        for (const auto& [pair, count] : confusions) {
            markdown << "| " << pair << " | " << count << " |\n";
        }
        markdown << '\n';
    }
    if (!report.gates.failures.empty()) {
        markdown << "## Gate failures\n\n";
        for (const auto& failure : report.gates.failures) {
            markdown << "- " << failure << '\n';
        }
    }
    return markdown.str();
}

bool writeLivePhonemeVerificationOutputs(
    const LivePhonemeVerificationReport& report,
    const std::filesystem::path& predictionsPath,
    const std::filesystem::path& metricsPath,
    const std::filesystem::path& reportPath,
    std::string& error)
{
    error.clear();
    if (!writeTextFile(predictionsPath, phonemeFramesToLabelJson(report.predictions), error)) {
        return false;
    }
    if (!writeTextFile(metricsPath, livePhonemeVerificationMetricsJson(report), error)) {
        return false;
    }
    return writeTextFile(reportPath, livePhonemeVerificationMarkdown(report), error);
}

} // namespace Voice2VocalSynth
