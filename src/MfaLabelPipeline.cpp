#include "Voice2VocalSynth/MfaLabelPipeline.h"

#include <cctype>
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

[[nodiscard]] std::string stripQuotes(std::string_view text)
{
    std::string value = trim(text);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

[[nodiscard]] double parseDouble(std::string_view text)
{
    return std::stod(std::string(trim(text)));
}

[[nodiscard]] std::optional<std::size_t> findPhonesTierStart(const std::vector<std::string>& lines)
{
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto line = trim(lines[index]);
        if (line.rfind("name =", 0) == 0) {
            const auto name = stripQuotes(line.substr(6));
            if (name == "phones" || name == "phone") {
                return index;
            }
        }
    }
    return std::nullopt;
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

std::string stripMfaArpabetStress(std::string_view label)
{
    std::string out;
    out.reserve(label.size());
    for (const char character : label) {
        if (std::isalpha(static_cast<unsigned char>(character))) {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
        }
    }

    while (!out.empty() && std::isdigit(static_cast<unsigned char>(out.back()))) {
        out.pop_back();
    }
    return out;
}

bool isSkippableMfaPhoneLabel(std::string_view label)
{
    const auto normalized = stripMfaArpabetStress(label);
    if (normalized.empty()) {
        return true;
    }
    if (normalized == "SIL" || normalized == "SPN" || normalized == "EPS") {
        return true;
    }
    return false;
}

MfaTextGridParseResult parseMfaPhonesTextGrid(std::string_view textGrid)
{
    MfaTextGridParseResult result;
    const auto lines = readLines(textGrid);
    const auto tierStart = findPhonesTierStart(lines);
    if (!tierStart) {
        result.error = "TextGrid is missing a phones interval tier";
        return result;
    }

    bool inInterval = false;
    double onset = 0.0;
    double end = 0.0;
    std::string phoneText;

    for (std::size_t index = *tierStart; index < lines.size(); ++index) {
        const auto line = trim(lines[index]);
        if (line.rfind("intervals [", 0) == 0) {
            if (inInterval && !isSkippableMfaPhoneLabel(phoneText)) {
                PhonemeFrame frame;
                frame.arpabet = stripMfaArpabetStress(phoneText);
                frame.confidence = 1.0F;
                frame.estimatedOnsetSeconds = onset;
                frame.estimatedEndSeconds = end;
                result.frames.push_back(std::move(frame));
            }
            inInterval = true;
            onset = 0.0;
            end = 0.0;
            phoneText.clear();
            continue;
        }
        if (!inInterval) {
            continue;
        }
        if (line.rfind("xmin =", 0) == 0) {
            onset = parseDouble(line.substr(6));
            continue;
        }
        if (line.rfind("xmax =", 0) == 0) {
            end = parseDouble(line.substr(6));
            continue;
        }
        if (line.rfind("text =", 0) == 0) {
            phoneText = stripQuotes(line.substr(6));
        }
    }

    if (inInterval && !isSkippableMfaPhoneLabel(phoneText)) {
        PhonemeFrame frame;
        frame.arpabet = stripMfaArpabetStress(phoneText);
        frame.confidence = 1.0F;
        frame.estimatedOnsetSeconds = onset;
        frame.estimatedEndSeconds = end;
        result.frames.push_back(std::move(frame));
    }

    if (result.frames.empty()) {
        result.error = "No phoneme intervals found in TextGrid";
        return result;
    }

    result.ok = true;
    return result;
}

std::string phonemeFramesToLabelJson(const std::vector<PhonemeFrame>& frames)
{
    std::ostringstream json;
    json.setf(std::ios::fixed);
    json.precision(6);
    json << "[\n";
    for (std::size_t index = 0; index < frames.size(); ++index) {
        const auto& frame = frames[index];
        json << "  {"
             << "\"arpabet\":\"" << jsonEscape(frame.arpabet) << "\","
             << "\"start\":" << frame.estimatedOnsetSeconds << ","
             << "\"end\":" << frame.estimatedEndSeconds << ","
             << "\"confidence\":" << frame.confidence << "}";
        if (index + 1 < frames.size()) {
            json << ',';
        }
        json << '\n';
    }
    json << "]\n";
    return json.str();
}

bool writePhonemeLabelJsonFile(const std::filesystem::path& path,
                               const std::vector<PhonemeFrame>& frames,
                               std::string& error)
{
    error.clear();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Unable to write label JSON: " + path.string();
        return false;
    }
    output << phonemeFramesToLabelJson(frames);
    return static_cast<bool>(output);
}

bool convertMfaTextGridTree(const std::filesystem::path& textGridRoot,
                            const std::filesystem::path& labelsRoot,
                            MfaTextGridConversionSummary& summary,
                            std::string& error)
{
    error.clear();
    summary = {};

    std::error_code ec;
    if (!std::filesystem::exists(textGridRoot, ec)) {
        error = "TextGrid root does not exist: " + textGridRoot.string();
        return false;
    }

    std::filesystem::create_directories(labelsRoot, ec);
    for (const auto& entry : std::filesystem::recursive_directory_iterator(textGridRoot, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }
        const auto extension = entry.path().extension().string();
        if (extension != ".TextGrid" && extension != ".textgrid") {
            continue;
        }
        ++summary.textGridCount;

        std::ifstream input(entry.path(), std::ios::binary);
        if (!input) {
            error = "Unable to read TextGrid: " + entry.path().string();
            return false;
        }
        std::ostringstream contents;
        contents << input.rdbuf();
        const auto parsed = parseMfaPhonesTextGrid(contents.str());
        if (!parsed.ok) {
            ++summary.skippedTextGrids;
            continue;
        }

        const auto labelPath = labelsRoot / (entry.path().stem().string() + ".json");
        if (!writePhonemeLabelJsonFile(labelPath, parsed.frames, error)) {
            return false;
        }
        ++summary.labelFileCount;
    }

    if (summary.labelFileCount == 0) {
        error = "No label JSON files were written from TextGrids under: " + textGridRoot.string();
        return false;
    }
    return true;
}

bool writeMfaLabelManifest(const MfaLabelManifestInfo& info,
                           const std::filesystem::path& manifestPath,
                           std::string& error)
{
    error.clear();
    std::error_code ec;
    std::filesystem::create_directories(manifestPath.parent_path(), ec);

    std::ofstream output(manifestPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Unable to write MFA label manifest: " + manifestPath.string();
        return false;
    }

    std::ostringstream json;
    json << "{\n";
    json << "  \"schemaVersion\": 1,\n";
    json << "  \"tool\": \"montreal-forced-aligner\",\n";
    json << "  \"generatedAt\": \"" << jsonEscape(formatLivePhonemeVerifyRunTimestamp()) << "\",\n";
    json << "  \"mfaVersion\": \"" << jsonEscape(info.mfaVersion) << "\",\n";
    json << "  \"acousticModel\": \"" << jsonEscape(info.acousticModel) << "\",\n";
    json << "  \"dictionary\": \"" << jsonEscape(info.dictionary) << "\",\n";
    json << "  \"subsetMode\": \"" << jsonEscape(info.subsetMode) << "\",\n";
    json << "  \"utteranceCount\": " << info.utteranceCount << ",\n";
    json << "  \"labelFileCount\": " << info.labelFileCount << ",\n";
    json << "  \"stressDigitsStripped\": true,\n";
    json << "  \"labelsDirectory\": \"" << jsonEscape(info.labelsDirectory.string()) << "\",\n";
    json << "  \"datasetRoot\": \"" << jsonEscape(info.datasetRoot.string()) << "\",\n";
    json << "  \"note\": \"MFA alignment parameters, dictionary/G2P tuning, and transcript "
            "normalization are initial defaults and expected to be refined in future verification "
            "runs.\"\n";
    json << "}\n";
    output << json.str();
    return static_cast<bool>(output);
}

} // namespace Voice2VocalSynth
