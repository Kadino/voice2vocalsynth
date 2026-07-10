#include "Voice2VocalSynth/LinuxVirtualAudio.h"

#include <cctype>
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

[[nodiscard]] std::optional<std::string> extractJsonStringField(std::string_view json,
                                                              std::string_view fieldName)
{
    const std::string needle = "\"" + std::string(fieldName) + "\"";
    const auto fieldPos = json.find(needle);
    if (fieldPos == std::string_view::npos) {
        return std::nullopt;
    }
    const auto colonPos = json.find(':', fieldPos + needle.size());
    if (colonPos == std::string_view::npos) {
        return std::nullopt;
    }
    const auto quoteBegin = json.find('"', colonPos + 1);
    if (quoteBegin == std::string_view::npos) {
        return std::nullopt;
    }
    const auto quoteEnd = json.find('"', quoteBegin + 1);
    if (quoteEnd == std::string_view::npos) {
        return std::nullopt;
    }
    return std::string(json.substr(quoteBegin + 1, quoteEnd - quoteBegin - 1));
}

[[nodiscard]] std::optional<bool> extractJsonBoolField(std::string_view json,
                                                       std::string_view fieldName)
{
    const std::string needle = "\"" + std::string(fieldName) + "\"";
    const auto fieldPos = json.find(needle);
    if (fieldPos == std::string_view::npos) {
        return std::nullopt;
    }
    const auto colonPos = json.find(':', fieldPos + needle.size());
    if (colonPos == std::string_view::npos) {
        return std::nullopt;
    }
    const auto value = trim(json.substr(colonPos + 1));
    if (value.rfind("true", 0) == 0) {
        return true;
    }
    if (value.rfind("false", 0) == 0) {
        return false;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> extractPactlServerName(std::string_view pactlInfo)
{
    for (const auto& line : readLines(pactlInfo)) {
        const auto trimmed = trim(line);
        if (trimmed.rfind("Server Name:", 0) == 0) {
            return trim(trimmed.substr(std::string_view("Server Name:").size()));
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool deviceNameExists(const std::vector<std::string>& names, std::string_view target)
{
    for (const auto& name : names) {
        if (name == target) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::optional<std::string> findAlsaLoopbackCard(std::string_view listing)
{
    for (const auto& line : readLines(listing)) {
        const auto trimmed = trim(line);
        if (trimmed.rfind("card ", 0) != 0) {
            continue;
        }
        if (trimmed.find("Loopback") == std::string::npos &&
            trimmed.find("loopback") == std::string::npos) {
            continue;
        }
        const auto colonPos = trimmed.find(':');
        if (colonPos == std::string::npos || colonPos <= 5) {
            continue;
        }
        return trim(trimmed.substr(5, colonPos - 5));
    }
    return std::nullopt;
}

} // namespace

std::filesystem::path defaultLinuxVirtualAudioManifestPath(const std::filesystem::path& verifyRoot)
{
    return verifyRoot / "linux-virtual-audio.json";
}

std::vector<std::string> parsePactlDeviceNames(std::string_view listing, std::string_view blockHeader)
{
    std::vector<std::string> names;
    bool inBlock = false;
    for (const auto& line : readLines(listing)) {
        const auto trimmed = trim(line);
        if (trimmed.rfind(std::string(blockHeader), 0) == 0) {
            inBlock = true;
            continue;
        }
        if (inBlock && trimmed.rfind("Name:", 0) == 0) {
            names.push_back(trim(trimmed.substr(5)));
            inBlock = false;
        }
    }
    return names;
}

std::string pactlMonitorSourceName(std::string_view sinkName)
{
    return std::string(sinkName) + ".monitor";
}

std::optional<LinuxVirtualAudioRoute> detectPipeWireLoopbackRoute(std::string_view pactlInfo,
                                                                  std::string_view pactlSinks,
                                                                  std::string_view pactlSources)
{
    const auto serverName = extractPactlServerName(pactlInfo);
    if (!serverName) {
        return std::nullopt;
    }

    const auto sinks = parsePactlDeviceNames(pactlSinks, "Sink #");
    const auto sources = parsePactlDeviceNames(pactlSources, "Source #");
    const std::string recommendedSink(kLinuxAudioRecommendedSinkName);
    const auto recommendedMonitor = pactlMonitorSourceName(recommendedSink);

    if (!deviceNameExists(sinks, recommendedSink) || !deviceNameExists(sources, recommendedMonitor)) {
        return std::nullopt;
    }

    LinuxVirtualAudioRoute route;
    route.routeId = std::string(kLinuxAudioRoutePipeWireLoopback);
    route.playbackDevice = recommendedSink;
    route.captureDevice = recommendedMonitor;
    route.serverName = *serverName;
    route.note = "Playback to the null sink is captured from its monitor source for the JUCE shell "
                 "input.";
    return route;
}

std::optional<LinuxVirtualAudioRoute> detectAlsaLoopbackRoute(std::string_view aplayListing,
                                                              std::string_view arecordListing)
{
    const auto playbackCard = findAlsaLoopbackCard(aplayListing);
    const auto captureCard = findAlsaLoopbackCard(arecordListing);
    if (!playbackCard || !captureCard) {
        return std::nullopt;
    }

    LinuxVirtualAudioRoute route;
    route.routeId = std::string(kLinuxAudioRouteAlsaSndAloop);
    route.playbackDevice = "plughw:" + *playbackCard + ",0,0";
    route.captureDevice = "plughw:" + *captureCard + ",1,0";
    route.serverName = "ALSA snd-aloop";
    route.note = "Route playback to loopback playback subdevice and capture from loopback capture "
                 "subdevice. Subdevice indices follow the common snd-aloop layout.";
    return route;
}

LinuxVirtualAudioCheckReportParseResult parseLinuxVirtualAudioCheckReport(std::string_view json)
{
    LinuxVirtualAudioCheckReportParseResult result;
    const auto valid = extractJsonBoolField(json, "valid");
    if (!valid) {
        result.error = "Check report JSON is missing boolean field 'valid'";
        return result;
    }

    result.validation.valid = *valid;
    if (const auto probePassed = extractJsonBoolField(json, "probePassed")) {
        result.validation.probePassed = *probePassed;
    }

    if (const auto route = extractJsonStringField(json, "route")) {
        result.validation.route.routeId = *route;
    }
    if (const auto playback = extractJsonStringField(json, "playbackDevice")) {
        result.validation.route.playbackDevice = *playback;
    }
    if (const auto capture = extractJsonStringField(json, "captureDevice")) {
        result.validation.route.captureDevice = *capture;
    }
    if (const auto server = extractJsonStringField(json, "serverName")) {
        result.validation.route.serverName = *server;
    }
    if (const auto note = extractJsonStringField(json, "note")) {
        result.validation.route.note = *note;
    }
    if (const auto error = extractJsonStringField(json, "error")) {
        result.validation.error = *error;
    }

    if (!result.validation.valid && result.validation.error.empty()) {
        result.error = "Check report marked invalid but did not include an error message";
        return result;
    }

    result.ok = true;
    return result;
}

bool writeLinuxVirtualAudioManifest(const LinuxVirtualAudioManifestInfo& info,
                                    const std::filesystem::path& manifestPath,
                                    std::string& error)
{
    error.clear();
    std::error_code ec;
    std::filesystem::create_directories(manifestPath.parent_path(), ec);

    std::ofstream output(manifestPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Unable to write Linux virtual audio manifest: " + manifestPath.string();
        return false;
    }

    std::ostringstream json;
    json << "{\n";
    json << "  \"schemaVersion\": 1,\n";
    json << "  \"generatedAt\": \"" << jsonEscape(formatLivePhonemeVerifyRunTimestamp()) << "\",\n";
    json << "  \"platform\": \"linux\",\n";
    json << "  \"primaryRoute\": \"" << jsonEscape(std::string(kLinuxAudioRoutePipeWireLoopback))
         << "\",\n";
    json << "  \"alternatives\": [\"alsa-snd-aloop\", \"jack\"],\n";
    json << "  \"selectedRoute\": \"" << jsonEscape(info.route.routeId) << "\",\n";
    json << "  \"playbackDevice\": \"" << jsonEscape(info.route.playbackDevice) << "\",\n";
    json << "  \"captureDevice\": \"" << jsonEscape(info.route.captureDevice) << "\",\n";
    json << "  \"serverName\": \"" << jsonEscape(info.route.serverName) << "\",\n";
    json << "  \"probePassed\": " << (info.probePassed ? "true" : "false") << ",\n";
    json << "  \"note\": \"" << jsonEscape(info.route.note) << "\",\n";
    json << "  \"setupScript\": \"scripts/validate_linux_virtual_audio.sh\"\n";
    json << "}\n";
    output << json.str();
    return static_cast<bool>(output);
}

LinuxVirtualAudioManifestLoadResult loadLinuxVirtualAudioManifest(std::string_view json)
{
    LinuxVirtualAudioManifestLoadResult result;
    const auto route = extractJsonStringField(json, "selectedRoute");
    const auto playback = extractJsonStringField(json, "playbackDevice");
    const auto capture = extractJsonStringField(json, "captureDevice");
    const auto server = extractJsonStringField(json, "serverName");
    const auto note = extractJsonStringField(json, "note");

    if (!route || !playback || !capture) {
        result.error = "Linux virtual audio manifest is missing route or device fields";
        return result;
    }

    result.info.route.routeId = *route;
    result.info.route.playbackDevice = *playback;
    result.info.route.captureDevice = *capture;
    if (server) {
        result.info.route.serverName = *server;
    }
    if (note) {
        result.info.route.note = *note;
    }
    if (const auto probePassed = extractJsonBoolField(json, "probePassed")) {
        result.info.probePassed = *probePassed;
    }

    result.ok = true;
    return result;
}

LinuxVirtualAudioManifestLoadResult loadLinuxVirtualAudioManifestFile(
    const std::filesystem::path& manifestPath)
{
    LinuxVirtualAudioManifestLoadResult result;
    std::ifstream input(manifestPath, std::ios::binary);
    if (!input) {
        result.error = "Unable to read Linux virtual audio manifest: " + manifestPath.string();
        return result;
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    return loadLinuxVirtualAudioManifest(contents.str());
}

} // namespace Voice2VocalSynth
