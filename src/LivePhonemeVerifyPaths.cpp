#include "Voice2VocalSynth/LivePhonemeVerifyPaths.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>

namespace Voice2VocalSynth
{
namespace
{

[[nodiscard]] std::string jsonEscape(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char character : text) {
        switch (character) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(character);
            break;
        }
    }
    return out;
}

[[nodiscard]] bool createDirectory(const std::filesystem::path& path, std::string& error)
{
    std::error_code ec;
    if (std::filesystem::create_directories(path, ec) || std::filesystem::exists(path)) {
        return true;
    }
    error = "Unable to create directory: " + path.string() + " (" + ec.message() + ")";
    return false;
}

} // namespace

std::filesystem::path defaultLivePhonemeVerifyRoot()
{
#if defined(_WIN32)
    if (const char* localAppData = std::getenv("LOCALAPPDATA")) {
        return std::filesystem::path(localAppData) / "Voice2VocalSynth" / "LivePhonemeVerify";
    }
#endif
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".local" / "share" / "Voice2VocalSynth" / "LivePhonemeVerify";
    }
    return std::filesystem::path("LivePhonemeVerify");
}

LivePhonemeVerifyLayout livePhonemeVerifyLayout(const std::filesystem::path& root)
{
    LivePhonemeVerifyLayout layout;
    layout.root = root;
    layout.datasets = root / "datasets";
    layout.labels = root / "labels";
    layout.runs = root / "runs";
    return layout;
}

bool ensureLivePhonemeVerifyLayout(const std::filesystem::path& root, std::string& error)
{
    error.clear();
    const auto layout = livePhonemeVerifyLayout(root);
    if (!createDirectory(layout.datasets, error)) {
        return false;
    }
    if (!createDirectory(layout.labels, error)) {
        return false;
    }
    if (!createDirectory(layout.runs, error)) {
        return false;
    }
    return true;
}

std::string formatLivePhonemeVerifyRunTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm utc {};
#if defined(_WIN32)
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    char buffer[32] {};
    std::strftime(buffer, sizeof(buffer), "%Y%m%dT%H%M%SZ", &utc);
    return buffer;
}

bool createLivePhonemeVerifyRun(const std::filesystem::path& root,
                                LivePhonemeVerifyRunPaths& out,
                                std::string& error)
{
    error.clear();
    if (!ensureLivePhonemeVerifyLayout(root, error)) {
        return false;
    }

    const auto layout = livePhonemeVerifyLayout(root);
    out = livePhonemeVerifyRunPaths(layout.runs / formatLivePhonemeVerifyRunTimestamp());

    if (!createDirectory(out.runDirectory, error)) {
        return false;
    }
    return true;
}

LivePhonemeVerifyRunPaths livePhonemeVerifyRunPaths(
    const std::filesystem::path& runDirectory)
{
    LivePhonemeVerifyRunPaths paths;
    paths.runDirectory = runDirectory;
    paths.liveLog = runDirectory / "live-log.jsonl";
    paths.manifest = runDirectory / "manifest.json";
    paths.playbackManifest = runDirectory / "playback-manifest.json";
    paths.predictions = runDirectory / "predictions.json";
    paths.metrics = runDirectory / "metrics.json";
    paths.report = runDirectory / "report.md";
    return paths;
}

bool writeLivePhonemeVerifyManifest(const LivePhonemeVerifyRunPaths& paths, std::string& error)
{
    error.clear();
    std::ofstream output(paths.manifest, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Unable to write manifest: " + paths.manifest.string();
        return false;
    }

    std::ostringstream json;
    json << "{\n";
    json << "  \"schemaVersion\": 1,\n";
    json << "  \"createdAt\": \"" << jsonEscape(formatLivePhonemeVerifyRunTimestamp()) << "\",\n";
    json << "  \"runDirectory\": \"" << jsonEscape(paths.runDirectory.string()) << "\",\n";
    json << "  \"liveLogFile\": \"" << jsonEscape(paths.liveLog.filename().string()) << "\",\n";
    json << "  \"verificationPlan\": \"docs/live-phoneme-verification-plan.md\",\n";
    json << "  \"backendTarget\": \"onnx_phoneme\",\n";
    json << "  \"labelPipeline\": {\n";
    json << "    \"tool\": \"montreal-forced-aligner\",\n";
    json << "    \"note\": \"MFA is the initial reference-label choice. Alignment parameters, "
            "dictionary/G2P tuning, and transcript normalization are expected to be refined in "
            "future verification runs.\"\n";
    json << "  },\n";
    json << "  \"linuxAudioRouting\": {\n";
    json << "    \"primary\": \"pipewire-loopback\",\n";
    json << "    \"alternatives\": [\"alsa-snd-aloop\", \"jack\"],\n";
    json << "    \"note\": \"PipeWire loopback is the first Linux routing target for local "
            "verification scripts. ALSA snd-aloop and JACK remain documented alternates for "
            "future host-specific support.\"\n";
    json << "  }\n";
    json << "}\n";
    output << json.str();
    return static_cast<bool>(output);
}

} // namespace Voice2VocalSynth
