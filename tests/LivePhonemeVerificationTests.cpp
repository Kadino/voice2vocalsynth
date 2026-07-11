#include <Voice2VocalSynth/LibriSpeechPlayback.h>
#include <Voice2VocalSynth/LivePhonemeVerification.h>
#include <Voice2VocalSynth/LivePhonemeVerifyCli.h>
#include <Voice2VocalSynth/MfaLabelPipeline.h>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
using namespace Voice2VocalSynth;

std::filesystem::path tempRoot()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("voice2vocalsynth-live-verify-" + std::to_string(stamp));
}

std::string sampleLog(std::string_view backend = "pocketsphinx_allphone")
{
    return "{\"kind\":\"session_start\",\"backend\":\"" + std::string(backend) +
           "\",\"steady_ns\":1000000000,\"stream_time_seconds\":0.0}\n"
           "{\"kind\":\"device_settings\",\"sample_rate_hz\":48000,"
           "\"buffer_samples\":256,\"input_latency_samples\":128,"
           "\"output_latency_samples\":128,\"input_buffer_samples\":256,"
           "\"output_buffer_samples\":256}\n"
           "{\"kind\":\"backend_inference\",\"backend\":\"" + std::string(backend) +
           "\",\"t_stream\":1.15,\"steady_ns\":2200000000,\"ok\":true,\"lag_ms\":10}\n"
           "{\"kind\":\"ph_frame\",\"backend\":\"" + std::string(backend) +
           "\",\"arpabet\":\"K\",\"conf\":0.8,\"t0\":1.10,\"t1\":1.15,"
           "\"steady_ns\":2200000000,\"vowel\":false,\"consonant\":true}\n";
}

LiveVerificationGateOptions passingGates()
{
    LiveVerificationGateOptions gates;
    gates.minF1 = 0.5;
    gates.maxMeanOnsetErrorMs = 50.0;
    gates.maxP95OnsetErrorMs = 50.0;
    gates.maxMeanEndErrorMs = 50.0;
    gates.maxP95EndErrorMs = 50.0;
    gates.maxMeanDurationErrorMs = 50.0;
    gates.maxMissedConsonantRate = 0.5;
    return gates;
}

void parsesAndConvertsLiveLog()
{
    const auto parsed = parseLivePhonemeLogJsonl(sampleLog());
    assert(parsed.ok);
    assert(parsed.log.sessionBackend == "pocketsphinx_allphone");
    assert(parsed.log.phonemeFrames.size() == 1);
    assert(parsed.log.backendLatencies.size() == 1);
    const auto converted = convertLivePhonemeFrames(parsed.log, "pocketsphinx");
    assert(converted.size() == 1);
    assert(converted[0].arpabet == "K");
}

void rejectsMixedLiveSessions()
{
    const auto parsed = parseLivePhonemeLogJsonl(
        "{\"kind\":\"session_start\",\"backend\":\"pocketsphinx_allphone\"}\n"
        "{\"kind\":\"session_start\",\"backend\":\"pocketsphinx_allphone\"}\n");
    assert(!parsed.ok);
}

void verifiesAlignedPlaybackAndWritesReports()
{
    const auto parsed = parseLivePhonemeLogJsonl(sampleLog());
    assert(parsed.ok);

    LibriSpeechPlaybackPlan playback;
    playback.playbackStartedSteadyNs = 2000000000;
    playback.clips.push_back(
        {"utt-1", "/tmp/utt-1.flac", 0.5, 0.0, 2000000000});
    playback.totalDurationSeconds = 0.5;

    const auto root = tempRoot();
    const auto labels = root / "labels";
    std::filesystem::create_directories(labels);
    PhonemeFrame reference;
    reference.arpabet = "K";
    reference.estimatedOnsetSeconds = 0.1;
    reference.estimatedEndSeconds = 0.15;
    std::string error;
    assert(writePhonemeLabelJsonFile(labels / "utt-1.json", {reference}, error));

    const auto verified =
        verifyLivePhonemeRun(parsed.log, playback, labels, "pocketsphinx", passingGates());
    assert(verified.ok);
    assert(verified.report.gates.passed);
    assert(verified.report.quality.f1 == 1.0);
    assert(verified.report.latency.backend.sampleCount == 1);

    assert(writeLivePhonemeVerificationOutputs(verified.report,
                                               root / "predictions.json",
                                               root / "metrics.json",
                                               root / "report.md",
                                               error));
    assert(std::filesystem::file_size(root / "predictions.json") > 0);
    assert(std::filesystem::file_size(root / "metrics.json") > 0);
    assert(std::filesystem::file_size(root / "report.md") > 0);
    std::filesystem::remove_all(root);
}

void rejectsPlaceholderAsPassingBackend()
{
    const auto parsed = parseLivePhonemeLogJsonl(sampleLog("placeholder_pitch_gate"));
    assert(parsed.ok);
    LibriSpeechPlaybackPlan playback;
    playback.playbackStartedSteadyNs = 2000000000;
    playback.clips.push_back(
        {"utt-1", "/tmp/utt-1.flac", 0.5, 0.0, 2000000000});

    const auto root = tempRoot();
    std::filesystem::create_directories(root);
    PhonemeFrame reference;
    reference.arpabet = "K";
    reference.estimatedOnsetSeconds = 0.1;
    reference.estimatedEndSeconds = 0.15;
    std::string error;
    assert(writePhonemeLabelJsonFile(root / "utt-1.json", {reference}, error));
    const auto verified =
        verifyLivePhonemeRun(parsed.log, playback, root, "placeholder", passingGates());
    assert(verified.ok);
    assert(!verified.report.gates.passed);
    assert(!verified.report.gates.failures.empty());
    std::filesystem::remove_all(root);
}

void requiresTemporalGateConfiguration()
{
    const auto parsed = parseLivePhonemeLogJsonl(sampleLog());
    assert(parsed.ok);
    LibriSpeechPlaybackPlan playback;
    playback.playbackStartedSteadyNs = 2000000000;
    playback.clips.push_back(
        {"utt-1", "/tmp/utt-1.flac", 0.5, 0.0, 2000000000});
    const auto root = tempRoot();
    std::filesystem::create_directories(root);
    PhonemeFrame reference;
    reference.arpabet = "K";
    reference.estimatedOnsetSeconds = 0.1;
    reference.estimatedEndSeconds = 0.15;
    std::string error;
    assert(writePhonemeLabelJsonFile(root / "utt-1.json", {reference}, error));
    const auto verified =
        verifyLivePhonemeRun(parsed.log, playback, root, "pocketsphinx", {});
    assert(verified.ok);
    assert(!verified.report.gates.passed);
    std::filesystem::remove_all(root);
}

void cliRejectsNonFiniteThresholds()
{
    const auto result = runLivePhonemeVerifyCli(
        {"Voice2VocalSynthLivePhonemeVerify",
         "--live-log", "/tmp/live.jsonl",
         "--playback-manifest", "/tmp/playback.json",
         "--labels-root", "/tmp/labels",
         "--min-f1", "nan"});
    assert(result.exitCode == LivePhonemeVerifyCliExitCode::Usage);
}

std::string onnxSampleLog()
{
    return "{\"kind\":\"session_start\",\"backend\":\"phoneme_onnx\","
           "\"steady_ns\":1000000000,\"stream_time_seconds\":0.0}\n"
           "{\"kind\":\"device_settings\",\"sample_rate_hz\":48000,"
           "\"buffer_samples\":256,\"input_latency_samples\":128,"
           "\"output_latency_samples\":128,\"input_buffer_samples\":256,"
           "\"output_buffer_samples\":256}\n"
           "{\"kind\":\"onnx\",\"t_stream\":1.15,\"steady_ns\":2200000000,"
           "\"lag_ms\":25,\"ok\":true}\n"
           "{\"kind\":\"ph_frame\",\"backend\":\"phoneme_onnx\","
           "\"arpabet\":\"AE\",\"conf\":0.8,\"t0\":1.10,\"t1\":1.15,"
           "\"steady_ns\":2200000000,\"vowel\":true,\"consonant\":false}\n";
}

void parsesOnnxLatencyLines()
{
    const auto parsed = parseLivePhonemeLogJsonl(onnxSampleLog());
    assert(parsed.ok);
    assert(parsed.log.sessionBackend == "phoneme_onnx");
    assert(parsed.log.backendLatencies.size() == 1);
    assert(parsed.log.backendLatencies[0].backend == "phoneme_onnx_async");
    assert(parsed.log.backendLatencies[0].lagMs == 25.0);

    const auto converted = convertLivePhonemeFrames(parsed.log, "onnx_phoneme");
    assert(converted.size() == 1);
    assert(converted[0].arpabet == "AE");
}

void computesE2eFromLatencyMeasure()
{
    std::string log = onnxSampleLog();
    log += "{\"kind\":\"latency_measure\",\"valid\":true,"
           "\"round_trip_ms\":80,\"estimate_ms\":120}\n";

    const auto parsed = parseLivePhonemeLogJsonl(log);
    assert(parsed.ok);
    assert(parsed.log.hasLatencyMeasurement);
    assert(parsed.log.latencyMeasurementValid);
    assert(parsed.log.measuredRoundTripMs == 80.0);

    LibriSpeechPlaybackPlan playback;
    playback.playbackStartedSteadyNs = 2000000000;
    playback.clips.push_back(
        {"utt-1", "/tmp/utt-1.flac", 0.5, 0.0, 2000000000});
    playback.totalDurationSeconds = 0.5;

    const auto root = tempRoot();
    const auto labels = root / "labels";
    std::filesystem::create_directories(labels);
    PhonemeFrame reference;
    reference.arpabet = "AE";
    reference.estimatedOnsetSeconds = 0.1;
    reference.estimatedEndSeconds = 0.15;
    std::string error;
    assert(writePhonemeLabelJsonFile(labels / "utt-1.json", {reference}, error));

    LiveVerificationGateOptions gates = passingGates();
    gates.maxEndToEndLatencyMs = 1000.0;
    const auto verified =
        verifyLivePhonemeRun(parsed.log, playback, labels, "onnx_phoneme", gates);
    assert(verified.ok);
    assert(verified.report.latency.endToEndSource == "loopback_plus_decision_p95");
    assert(verified.report.latency.endToEndMs > 80.0);
    assert(verified.report.backend == "phoneme_onnx");

    std::filesystem::remove_all(root);
}

void scoresMultiClipPlayback()
{
    const auto parsed = parseLivePhonemeLogJsonl(sampleLog());
    assert(parsed.ok);

    LibriSpeechPlaybackPlan playback;
    playback.playbackStartedSteadyNs = 2000000000;
    playback.clips.push_back(
        {"utt-1", "/tmp/utt-1.flac", 0.5, 0.0, 2000000000});
    playback.clips.push_back(
        {"utt-2", "/tmp/utt-2.flac", 0.4, 1.0, 3000000000});
    playback.totalDurationSeconds = 1.4;

    const auto root = tempRoot();
    const auto labels = root / "labels";
    std::filesystem::create_directories(labels);
    PhonemeFrame reference;
    reference.arpabet = "K";
    reference.estimatedOnsetSeconds = 0.1;
    reference.estimatedEndSeconds = 0.15;
    std::string error;
    assert(writePhonemeLabelJsonFile(labels / "utt-1.json", {reference}, error));
    assert(writePhonemeLabelJsonFile(labels / "utt-2.json", {reference}, error));

    const auto verified =
        verifyLivePhonemeRun(parsed.log, playback, labels, "pocketsphinx", passingGates());
    assert(verified.ok);
    assert(verified.report.utterances.size() == 2);

    std::filesystem::remove_all(root);
}

} // namespace

int main()
{
    parsesAndConvertsLiveLog();
    rejectsMixedLiveSessions();
    verifiesAlignedPlaybackAndWritesReports();
    rejectsPlaceholderAsPassingBackend();
    requiresTemporalGateConfiguration();
    cliRejectsNonFiniteThresholds();
    parsesOnnxLatencyLines();
    computesE2eFromLatencyMeasure();
    scoresMultiClipPlayback();
    std::cout << "LivePhonemeVerification tests passed\n";
    return 0;
}
