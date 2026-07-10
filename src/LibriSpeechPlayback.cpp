#include "Voice2VocalSynth/LibriSpeechPlayback.h"

#include <cctype>
#include <cmath>
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

[[nodiscard]] std::string trim(std::string_view text)
{
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

[[nodiscard]] std::vector<std::string> readLines(std::string_view text)
{
    std::vector<std::string> lines;
    std::istringstream input{std::string(text)};
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

} // namespace

std::filesystem::path defaultLibriSpeechPlaybackManifestPath(
    const std::filesystem::path& runDirectory)
{
    return runDirectory / "playback-manifest.json";
}

std::vector<LibriSpeechPlaybackDurationEntry> parseLibriSpeechDurationTsv(std::string_view text,
                                                                          std::string& error)
{
    error.clear();
    std::vector<LibriSpeechPlaybackDurationEntry> entries;
    for (const auto& line : readLines(text)) {
        if (trim(line).empty()) {
            continue;
        }
        const auto tab = line.find('\t');
        if (tab == std::string::npos) {
            error = "Duration TSV must use utterance-id<TAB>duration-seconds format";
            return {};
        }
        LibriSpeechPlaybackDurationEntry entry;
        entry.utteranceId = trim(line.substr(0, tab));
        try {
            entry.durationSeconds = std::stod(std::string(trim(line.substr(tab + 1))));
        } catch (const std::exception&) {
            error = "Invalid duration for utterance: " + entry.utteranceId;
            return {};
        }
        if (entry.utteranceId.empty() || entry.durationSeconds <= 0.0) {
            error = "Duration TSV entries must include a positive duration";
            return {};
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

LibriSpeechPlaybackBuildResult buildLibriSpeechPlaybackPlan(
    const std::vector<LibriSpeechUtterance>& utterances,
    const std::vector<LibriSpeechPlaybackDurationEntry>& durations,
    const LinuxVirtualAudioRoute& route,
    const double gapSeconds,
    const std::filesystem::path& runDirectory)
{
    LibriSpeechPlaybackBuildResult result;
    if (utterances.empty()) {
        result.error = "No utterances selected for playback";
        return result;
    }
    if (gapSeconds < 0.0) {
        result.error = "Gap seconds must be non-negative";
        return result;
    }
    if (route.playbackDevice.empty() || route.routeId.empty()) {
        result.error = "Playback route is missing device or route id";
        return result;
    }

    std::unordered_map<std::string, double> durationById;
    durationById.reserve(durations.size());
    for (const auto& entry : durations) {
        durationById.emplace(entry.utteranceId, entry.durationSeconds);
    }

    result.plan.gapSeconds = gapSeconds;
    result.plan.playbackDevice = route.playbackDevice;
    result.plan.routeId = route.routeId;
    result.plan.runDirectory = runDirectory;
    result.plan.clips.reserve(utterances.size());

    double timelineSeconds = 0.0;
    for (std::size_t index = 0; index < utterances.size(); ++index) {
        const auto& utterance = utterances[index];
        const auto durationIt = durationById.find(utterance.id);
        if (durationIt == durationById.end()) {
            result.error = "Missing duration for utterance: " + utterance.id;
            return result;
        }

        LibriSpeechPlaybackClip clip;
        clip.utteranceId = utterance.id;
        clip.flacPath = utterance.flacPath;
        clip.durationSeconds = durationIt->second;
        clip.startOffsetSeconds = timelineSeconds;
        result.plan.clips.push_back(std::move(clip));

        timelineSeconds += durationIt->second;
        if (index + 1 < utterances.size()) {
            timelineSeconds += gapSeconds;
        }
    }

    result.plan.totalDurationSeconds = timelineSeconds;
    result.ok = true;
    return result;
}

bool writeLibriSpeechPlaybackManifest(const LibriSpeechPlaybackPlan& plan,
                                      const std::filesystem::path& manifestPath,
                                      std::string& error)
{
    error.clear();
    std::error_code ec;
    std::filesystem::create_directories(manifestPath.parent_path(), ec);

    std::ofstream output(manifestPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Unable to write playback manifest: " + manifestPath.string();
        return false;
    }

    std::ostringstream json;
    json.setf(std::ios::fixed);
    json.precision(6);
    json << "{\n";
    json << "  \"schemaVersion\": 1,\n";
    json << "  \"generatedAt\": \"" << jsonEscape(formatLivePhonemeVerifyRunTimestamp()) << "\",\n";
    json << "  \"realtime\": true,\n";
    json << "  \"routeId\": \"" << jsonEscape(plan.routeId) << "\",\n";
    json << "  \"playbackDevice\": \"" << jsonEscape(plan.playbackDevice) << "\",\n";
    json << "  \"gapSeconds\": " << plan.gapSeconds << ",\n";
    json << "  \"totalDurationSeconds\": " << plan.totalDurationSeconds << ",\n";
    json << "  \"runDirectory\": \"" << jsonEscape(plan.runDirectory.string()) << "\",\n";
    json << "  \"clips\": [\n";
    for (std::size_t index = 0; index < plan.clips.size(); ++index) {
        const auto& clip = plan.clips[index];
        json << "    {"
             << "\"utteranceId\":\"" << jsonEscape(clip.utteranceId) << "\","
             << "\"flacPath\":\"" << jsonEscape(clip.flacPath.string()) << "\","
             << "\"durationSeconds\":" << clip.durationSeconds << ","
             << "\"startOffsetSeconds\":" << clip.startOffsetSeconds << "}";
        if (index + 1 < plan.clips.size()) {
            json << ',';
        }
        json << '\n';
    }
    json << "  ]\n";
    json << "}\n";
    output << json.str();
    return static_cast<bool>(output);
}

} // namespace Voice2VocalSynth
