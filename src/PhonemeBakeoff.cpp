#include "Voice2VocalSynth/PhonemeBakeoff.h"
#include "Voice2VocalSynth/PhonemeFallbackMapper.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace Voice2VocalSynth
{
namespace
{

[[nodiscard]] std::size_t windowSamplesForBackend(const PhonemeBackendDescriptor& descriptor, double sampleRateHz)
{
    if (descriptor.inputKind == PhonemeBackendInputKind::FlatTensor) {
        return 32;
    }
    if (descriptor.windowMs > 0.0 && sampleRateHz > 0.0) {
        return static_cast<std::size_t>(
            std::max(1.0, static_cast<double>(std::llround(descriptor.windowMs * sampleRateHz / 1000.0))));
    }
    return static_cast<std::size_t>(
        std::max(1.0, static_cast<double>(std::llround(sampleRateHz * 0.085))));
}

[[nodiscard]] std::size_t hopSamplesForBackend(const PhonemeBackendDescriptor& descriptor, double sampleRateHz)
{
    if (descriptor.hopMs > 0.0 && sampleRateHz > 0.0) {
        return static_cast<std::size_t>(
            std::max(1.0, static_cast<double>(std::llround(descriptor.hopMs * sampleRateHz / 1000.0))));
    }
    return windowSamplesForBackend(descriptor, sampleRateHz);
}

[[nodiscard]] double meanValue(const std::vector<double>& values)
{
    if (values.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const double value : values) {
        sum += value;
    }
    return sum / static_cast<double>(values.size());
}

} // namespace

PhonemeFrame phonemeObservationToFrame(const PhonemeTemporalObservation& observation,
                                         const double segmentSeconds)
{
    PhonemeFrame frame;
    frame.arpabet = observation.arpabet;
    frame.confidence = observation.confidence;
    frame.estimatedEndSeconds = observation.stream_time_seconds;
    frame.estimatedOnsetSeconds = observation.stream_time_seconds - std::max(0.0, segmentSeconds);
    frame.isVowel = PhonemeFallbackMapper::isArpabetVowel(frame.arpabet);
    frame.isConsonant = !frame.arpabet.empty() && !frame.isVowel;
    frame.isVoiced = frame.isVowel || frame.isConsonant;
    return frame;
}

PhonemeBackendPipelineResult runPhonemeBackendOnMono(
    IPhonemeBackend& backend,
    const std::vector<float>& mono,
    const double sampleRateHz,
    const PhonemeTemporalStabilizerOptions& stabilizerOptions)
{
    PhonemeBackendPipelineResult result;
    result.backendName = backend.name();

    if (sampleRateHz <= 0.0 || mono.empty()) {
        result.error = "Mono PCM input must be non-empty with a positive sample rate";
        return result;
    }

    const auto descriptor = backend.descriptor();
    const auto windowSamples = windowSamplesForBackend(descriptor, sampleRateHz);
    const auto hopSamples = hopSamplesForBackend(descriptor, sampleRateHz);
    const double segmentSeconds = static_cast<double>(windowSamples) / sampleRateHz;

    PhonemeTemporalStabilizer stabilizer(stabilizerOptions);
    for (std::size_t start = 0; start + windowSamples <= mono.size(); start += hopSamples) {
        PhonemeBackendAudioFrame frame;
        frame.sampleRateHz = sampleRateHz;
        frame.streamTimeStartSeconds = static_cast<double>(start) / sampleRateHz;
        frame.monoSamples.assign(mono.begin() + static_cast<std::ptrdiff_t>(start),
                                 mono.begin() + static_cast<std::ptrdiff_t>(start + windowSamples));

        const auto backendResult = backend.process(frame);
        if (!backendResult.ok) {
            result.error = backendResult.error;
            return result;
        }
        if (backendResult.backendLatencyMs > 0.0) {
            result.backendLatenciesMs.push_back(backendResult.backendLatencyMs);
        }

        for (const auto& observation : backendResult.observations) {
            if (!observation.arpabet.empty()) {
                result.rawFrames.push_back(phonemeObservationToFrame(observation, segmentSeconds));
            }
            stabilizer.observe(observation);
        }
    }

    PhonemeFrame committed;
    while (stabilizer.try_pop_committed(committed)) {
        result.stabilizedFrames.push_back(std::move(committed));
    }

    result.meanBackendLatencyMs = meanValue(result.backendLatenciesMs);
    result.p95BackendLatencyMs = percentileSeconds(result.backendLatenciesMs, 0.95);
    result.ok = true;
    return result;
}

PhonemeBakeoffReport runPhonemeBakeoff(
    const std::vector<PhonemeFrame>& reference,
    const std::vector<IPhonemeBackend*>& backends,
    const std::vector<float>& mono,
    const double sampleRateHz,
    const PhonemeEvaluationOptions& options,
    const PhonemeTemporalStabilizerOptions& stabilizerOptions)
{
    PhonemeBakeoffReport report;
    for (auto* backend : backends) {
        if (backend == nullptr) {
            continue;
        }

        const auto pipeline = runPhonemeBackendOnMono(*backend, mono, sampleRateHz, stabilizerOptions);
        PhonemeBakeoffEntry entry;
        entry.backendName = backend->name();
        entry.meanBackendLatencyMs = pipeline.meanBackendLatencyMs;
        entry.p95BackendLatencyMs = pipeline.p95BackendLatencyMs;
        if (pipeline.ok) {
            entry.rawMetrics = evaluatePhonemeFrames(reference, pipeline.rawFrames, options);
            entry.stabilizedMetrics =
                evaluatePhonemeFrames(reference, pipeline.stabilizedFrames, options);
        }
        report.entries.push_back(std::move(entry));
    }
    return report;
}

std::string phonemeBakeoffReportToJson(const PhonemeBakeoffReport& report)
{
    auto writeMetrics = [](std::ostringstream& json, const PhonemeEvaluationMetrics& metrics) {
        json << "        \"referenceCount\": " << metrics.referenceCount << ",\n";
        json << "        \"predictionCount\": " << metrics.predictionCount << ",\n";
        json << "        \"matchedCount\": " << metrics.matchedCount << ",\n";
        json << "        \"missedCount\": " << metrics.missedCount << ",\n";
        json << "        \"falsePositiveCount\": " << metrics.falsePositiveCount << ",\n";
        json << "        \"referenceConsonantCount\": " << metrics.referenceConsonantCount << ",\n";
        json << "        \"missedConsonantCount\": " << metrics.missedConsonantCount << ",\n";
        json << "        \"precision\": " << metrics.precision << ",\n";
        json << "        \"recall\": " << metrics.recall << ",\n";
        json << "        \"f1\": " << metrics.f1 << ",\n";
        json << "        \"falsePositiveRate\": " << metrics.falsePositiveRate << ",\n";
        json << "        \"missedConsonantRate\": " << metrics.missedConsonantRate << ",\n";
        json << "        \"meanAbsoluteOnsetErrorMs\": " << metrics.meanAbsoluteOnsetErrorMs << ",\n";
        json << "        \"p95OnsetErrorMs\": " << metrics.p95OnsetErrorMs;
    };

    std::ostringstream json;
    json << std::fixed;
    json.precision(6);
    json << "{\n  \"schemaVersion\": 1,\n  \"entries\": [\n";
    for (std::size_t index = 0; index < report.entries.size(); ++index) {
        const auto& entry = report.entries[index];
        json << "    {\n";
        json << "      \"backendName\": \"" << entry.backendName << "\",\n";
        json << "      \"meanBackendLatencyMs\": " << entry.meanBackendLatencyMs << ",\n";
        json << "      \"p95BackendLatencyMs\": " << entry.p95BackendLatencyMs << ",\n";
        json << "      \"rawMetrics\": {\n";
        writeMetrics(json, entry.rawMetrics);
        json << "\n      },\n";
        json << "      \"stabilizedMetrics\": {\n";
        writeMetrics(json, entry.stabilizedMetrics);
        json << "\n      }\n";
        json << "    }";
        if (index + 1 < report.entries.size()) {
            json << ',';
        }
        json << '\n';
    }
    json << "  ]\n}\n";
    return json.str();
}

} // namespace Voice2VocalSynth
