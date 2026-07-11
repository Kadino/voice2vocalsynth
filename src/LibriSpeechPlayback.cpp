#include "Voice2VocalSynth/LibriSpeechPlayback.h"

#include <cctype>
#include <cmath>
#include <fstream>
#include <optional>
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

[[nodiscard]] std::size_t valuePosition(std::string_view json, std::string_view key)
{
    const auto keyPosition = json.find("\"" + std::string(key) + "\"");
    if (keyPosition == std::string_view::npos) {
        return std::string_view::npos;
    }
    auto position = json.find(':', keyPosition + key.size() + 2);
    if (position == std::string_view::npos) {
        return position;
    }
    ++position;
    while (position < json.size() &&
           std::isspace(static_cast<unsigned char>(json[position]))) {
        ++position;
    }
    return position;
}

[[nodiscard]] std::optional<std::string> jsonString(std::string_view json,
                                                    std::string_view key)
{
    auto position = valuePosition(json, key);
    if (position == std::string_view::npos || position >= json.size() || json[position] != '"') {
        return std::nullopt;
    }
    ++position;
    std::string value;
    bool escaped = false;
    for (; position < json.size(); ++position) {
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

[[nodiscard]] std::optional<double> jsonNumber(std::string_view json,
                                               std::string_view key)
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

[[nodiscard]] std::vector<std::string_view> jsonObjectArray(std::string_view json,
                                                            std::string_view key)
{
    std::vector<std::string_view> objects;
    auto position = valuePosition(json, key);
    if (position == std::string_view::npos || position >= json.size() || json[position] != '[') {
        return objects;
    }
    bool inString = false;
    bool escaped = false;
    int depth = 0;
    std::size_t objectStart = std::string_view::npos;
    for (++position; position < json.size(); ++position) {
        const char character = json[position];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                inString = false;
            }
            continue;
        }
        if (character == '"') {
            inString = true;
        } else if (character == '{') {
            if (depth++ == 0) {
                objectStart = position;
            }
        } else if (character == '}') {
            if (--depth == 0 && objectStart != std::string_view::npos) {
                objects.push_back(json.substr(objectStart, position - objectStart + 1));
                objectStart = std::string_view::npos;
            }
        } else if (character == ']' && depth == 0) {
            break;
        }
    }
    return objects;
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
    json << "  \"playbackStartedSteadyNs\": " << plan.playbackStartedSteadyNs << ",\n";
    json << "  \"runDirectory\": \"" << jsonEscape(plan.runDirectory.string()) << "\",\n";
    json << "  \"clips\": [\n";
    for (std::size_t index = 0; index < plan.clips.size(); ++index) {
        const auto& clip = plan.clips[index];
        json << "    {"
             << "\"utteranceId\":\"" << jsonEscape(clip.utteranceId) << "\","
             << "\"flacPath\":\"" << jsonEscape(clip.flacPath.string()) << "\","
             << "\"durationSeconds\":" << clip.durationSeconds << ","
             << "\"startOffsetSeconds\":" << clip.startOffsetSeconds << ","
             << "\"playbackStartedSteadyNs\":" << clip.playbackStartedSteadyNs << "}";
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

LibriSpeechPlaybackManifestLoadResult parseLibriSpeechPlaybackManifest(
    std::string_view json)
{
    LibriSpeechPlaybackManifestLoadResult result;
    const auto route = jsonString(json, "routeId");
    const auto playback = jsonString(json, "playbackDevice");
    const auto gap = jsonNumber(json, "gapSeconds");
    const auto duration = jsonNumber(json, "totalDurationSeconds");
    if (!route || !playback || !gap || !duration) {
        result.error = "Playback manifest is missing route or timing fields";
        return result;
    }
    result.plan.routeId = *route;
    result.plan.playbackDevice = *playback;
    result.plan.gapSeconds = *gap;
    result.plan.totalDurationSeconds = *duration;
    if (const auto runDirectory = jsonString(json, "runDirectory")) {
        result.plan.runDirectory = *runDirectory;
    }
    if (const auto started = jsonNumber(json, "playbackStartedSteadyNs")) {
        result.plan.playbackStartedSteadyNs = static_cast<std::int64_t>(*started);
    }

    for (const auto object : jsonObjectArray(json, "clips")) {
        const auto id = jsonString(object, "utteranceId");
        const auto flac = jsonString(object, "flacPath");
        const auto clipDuration = jsonNumber(object, "durationSeconds");
        const auto offset = jsonNumber(object, "startOffsetSeconds");
        if (!id || !flac || !clipDuration || !offset || id->empty() || *clipDuration <= 0.0) {
            result.error = "Playback manifest contains an invalid clip";
            result.plan.clips.clear();
            return result;
        }
        const auto started = jsonNumber(object, "playbackStartedSteadyNs").value_or(0.0);
        result.plan.clips.push_back(
            {*id, *flac, *clipDuration, *offset, static_cast<std::int64_t>(started)});
    }
    if (result.plan.clips.empty()) {
        result.error = "Playback manifest contains no clips";
        return result;
    }
    result.ok = true;
    return result;
}

LibriSpeechPlaybackManifestLoadResult loadLibriSpeechPlaybackManifest(
    const std::filesystem::path& manifestPath)
{
    std::ifstream input(manifestPath, std::ios::binary);
    if (!input) {
        LibriSpeechPlaybackManifestLoadResult result;
        result.error = "Unable to open playback manifest: " + manifestPath.string();
        return result;
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    return parseLibriSpeechPlaybackManifest(contents.str());
}

} // namespace Voice2VocalSynth
