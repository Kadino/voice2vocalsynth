#include "Voice2VocalSynth/LiveLogFixture.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace Voice2VocalSynth
{
namespace
{

[[nodiscard]] std::string jsonEscape(std::string_view value)
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

[[nodiscard]] double clipStartStreamSeconds(const LiveLogFixtureOptions& options)
{
    return options.sessionStreamTimeSeconds +
           static_cast<double>(options.playbackAnchorSteadyNs - options.sessionSteadyNs) / 1.0e9;
}

void appendBackendDescriptor(std::ostringstream& output, std::string_view sessionBackend)
{
    output << std::fixed << std::setprecision(1);
    output << "{\"kind\":\"backend_descriptor\",\"backend\":\""
           << jsonEscape(sessionBackend) << "\"";
    if (sessionBackend == "pocketsphinx_allphone") {
        output << ",\"sample_rate_hz\":16000,\"window_ms\":25.0,\"hop_ms\":10.0"
               << ",\"timestamp_semantics\":\"frame_end_seconds\""
               << ",\"labels\":[\"sil\",\"AA\",\"AE\",\"AH\",\"AO\",\"AW\",\"AY\",\"B\",\"CH\","
               "\"D\",\"DH\",\"EH\",\"ER\",\"EY\",\"F\",\"G\",\"HH\",\"IH\",\"IY\",\"JH\",\"K\","
               "\"L\",\"M\",\"N\",\"NG\",\"OW\",\"OY\",\"P\",\"R\",\"S\",\"SH\",\"T\",\"TH\","
               "\"UH\",\"UW\",\"V\",\"W\",\"Y\",\"Z\",\"ZH\"]}";
    } else if (sessionBackend == "phoneme_onnx") {
        output << ",\"sample_rate_hz\":48000,\"window_ms\":85.0,\"hop_ms\":85.0"
               << ",\"timestamp_semantics\":\"frame_end_seconds\""
               << ",\"labels\":[\"sil\",\"AH\"]}";
    } else {
        output << ",\"sample_rate_hz\":48000,\"window_ms\":85.0,\"hop_ms\":85.0"
               << ",\"timestamp_semantics\":\"frame_end_seconds\""
               << ",\"labels\":[\"sil\",\"AH\"]}";
    }
    output << "}\n";
}

} // namespace

std::string liveLogFixtureSessionBackend(std::string_view backendCli)
{
    if (backendCli == "pocketsphinx") {
        return "pocketsphinx_allphone";
    }
    if (backendCli == "onnx_phoneme" || backendCli == "onnx") {
        return "phoneme_onnx";
    }
    if (backendCli == "placeholder") {
        return "placeholder_pitch_gate";
    }
    return std::string(backendCli);
}

std::vector<LiveLogFixturePhonemeSegment> defaultMfaSamplePhonemeSegments()
{
    return {
        {"K", 0.05, 0.20, false, true, 0.85F},
        {"AE", 0.20, 0.45, true, false, 0.85F},
        {"T", 0.45, 0.55, false, true, 0.85F},
    };
}

LiveLogFixtureContent generateLiveLogFixture(const LiveLogFixtureOptions& options)
{
    const auto sessionBackend = liveLogFixtureSessionBackend(options.backendCli);
    const auto clipStart = clipStartStreamSeconds(options);
    const auto segments =
        options.phonemeSegments.empty() ? defaultMfaSamplePhonemeSegments() : options.phonemeSegments;

    std::ostringstream output;
    output << std::fixed << std::setprecision(5);

    output << "{\"kind\":\"session_start\",\"live_log_export\":true"
           << ",\"run_directory\":\"" << jsonEscape(options.runPaths.runDirectory.string()) << "\""
           << ",\"manifest\":\"" << jsonEscape(options.runPaths.manifest.string()) << "\""
           << ",\"backend\":\"" << jsonEscape(sessionBackend) << "\""
           << ",\"startup_ok\":" << (options.startupOk ? "true" : "false")
           << ",\"startup_error\":\"" << jsonEscape(options.startupError) << "\""
           << ",\"steady_ns\":" << options.sessionSteadyNs
           << ",\"stream_time_seconds\":" << options.sessionStreamTimeSeconds << "}\n";

    appendBackendDescriptor(output, sessionBackend);

    output << std::setprecision(3);
    output << "{\"kind\":\"device_settings\""
           << ",\"device_name\":\"" << jsonEscape(options.captureDevice) << "\""
           << ",\"sample_rate_hz\":48000"
           << ",\"buffer_samples\":256"
           << ",\"input_latency_samples\":128"
           << ",\"output_latency_samples\":128"
           << ",\"input_buffer_samples\":256"
           << ",\"output_buffer_samples\":256}\n";

    output << std::setprecision(5);
    std::int64_t steadyNs = options.sessionSteadyNs;
    for (const auto& segment : segments) {
        const double inferenceStream = clipStart + segment.onsetSeconds;
        const double segmentEndStream = clipStart + segment.endSeconds;
        steadyNs = options.sessionSteadyNs +
                   static_cast<std::int64_t>((inferenceStream - options.sessionStreamTimeSeconds) *
                                             1.0e9) +
                   25'000'000;
        output << "{\"kind\":\"backend_inference\",\"backend\":\"" << jsonEscape(sessionBackend)
               << "\",\"t_stream\":" << inferenceStream << ",\"steady_ns\":" << steadyNs
               << ",\"ok\":true,\"lag_ms\":12.5}\n";

        steadyNs = options.sessionSteadyNs +
                   static_cast<std::int64_t>((segmentEndStream - options.sessionStreamTimeSeconds) *
                                             1.0e9) +
                   40'000'000;
        const double t0 = clipStart + segment.onsetSeconds;
        const double t1 = segmentEndStream;
        output << "{\"kind\":\"ph_frame\",\"backend\":\"" << jsonEscape(sessionBackend)
               << "\",\"arpabet\":\"" << jsonEscape(segment.arpabet) << "\",\"conf\":"
               << segment.confidence << ",\"t0\":" << t0 << ",\"t1\":" << t1
               << ",\"steady_ns\":" << steadyNs
               << ",\"vowel\":" << (segment.vowel ? "true" : "false")
               << ",\"consonant\":" << (segment.consonant ? "true" : "false") << "}\n";
    }

    if (options.includeOnnxLatency && sessionBackend == "phoneme_onnx") {
        output << "{\"kind\":\"onnx\",\"job\":1,\"t_stream\":" << (clipStart + 0.15)
               << ",\"steady_ns\":" << steadyNs << ",\"ok\":true,\"lag_ms\":25.0"
               << ",\"lag_est_ms\":24.0}\n";
        steadyNs += 50'000'000;
    }

    if (options.includeLatencyMeasure) {
        output << std::setprecision(3);
        output << "{\"kind\":\"latency_measure\",\"valid\":true"
               << ",\"round_trip_ms\":80.0,\"estimate_ms\":120.0}\n";
    }

    LiveLogFixtureContent content;
    content.jsonl = output.str();
    return content;
}

bool writeLiveLogFixture(const LiveLogFixtureOptions& options, std::string& error)
{
    error.clear();
    std::error_code ec;
    std::filesystem::create_directories(options.runPaths.runDirectory, ec);

    const auto content = generateLiveLogFixture(options);
    std::ofstream output(options.runPaths.liveLog, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Unable to write live log fixture: " + options.runPaths.liveLog.string();
        return false;
    }
    output << content.jsonl;
    if (!output) {
        error = "Failed while writing live log fixture: " + options.runPaths.liveLog.string();
        return false;
    }
    return true;
}

} // namespace Voice2VocalSynth
