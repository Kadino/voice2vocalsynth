#include "Voice2VocalSynth/AppSettings.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <variant>

namespace Voice2VocalSynth
{
namespace
{

std::string trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string normalizePathForWindowsComparison(std::string value)
{
    std::replace(value.begin(), value.end(), '\\', '/');

    while (value.size() > 1 && value.back() == '/') {
        value.pop_back();
    }

    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    return value;
}

void addWarningIfEmpty(std::vector<std::string>& warnings,
                       const std::string& value,
                       const char* message)
{
    if (trim(value).empty()) {
        warnings.emplace_back(message);
    }
}

std::string escapeJsonString(const std::string& value)
{
    std::ostringstream output;
    for (const auto character : value) {
        switch (character) {
            case '\\':
                output << "\\\\";
                break;
            case '"':
                output << "\\\"";
                break;
            case '\n':
                output << "\\n";
                break;
            case '\r':
                output << "\\r";
                break;
            case '\t':
                output << "\\t";
                break;
            default:
                output << character;
                break;
        }
    }
    return output.str();
}

const char* toJsonString(OutputRoute route)
{
    switch (route) {
        case OutputRoute::MonitorOutput:
            return "monitor";
        case OutputRoute::ProjectVirtualMicrophone:
            return "projectVirtualMicrophone";
    }
    return "monitor";
}

const char* toJsonString(AliasStylePreference preference)
{
    switch (preference) {
        case AliasStylePreference::AutoDetect:
            return "auto";
        case AliasStylePreference::PreferRomaji:
            return "preferRomaji";
        case AliasStylePreference::PreferNonAscii:
            return "preferNonAscii";
    }
    return "auto";
}

const char* toJsonString(PitchMode mode)
{
    switch (mode) {
        case PitchMode::FollowInput:
            return "followInput";
        case PitchMode::SnapToNearestSemitone:
            return "snapToNearestSemitone";
        case PitchMode::SnapToKey:
            return "snapToKey";
        case PitchMode::FixedDefault:
            return "fixedDefault";
    }
    return "followInput";
}

const char* toJsonString(ScaleType scale)
{
    switch (scale) {
        case ScaleType::Major:
            return "major";
        case ScaleType::NaturalMinor:
            return "naturalMinor";
        case ScaleType::MajorPentatonic:
            return "majorPentatonic";
        case ScaleType::MinorPentatonic:
            return "minorPentatonic";
        case ScaleType::Chromatic:
            return "chromatic";
    }
    return "major";
}

const char* toJsonString(LowConfidencePitchBehavior behavior)
{
    switch (behavior) {
        case LowConfidencePitchBehavior::UseRecentMean:
            return "useRecentMean";
        case LowConfidencePitchBehavior::UseDefaultPitch:
            return "useDefaultPitch";
    }
    return "useRecentMean";
}

const char* toJsonString(LatencyPreset preset)
{
    switch (preset) {
        case LatencyPreset::LowLatency:
            return "lowLatency";
        case LatencyPreset::Balanced:
            return "balanced";
        case LatencyPreset::HighAccuracy:
            return "highAccuracy";
        case LatencyPreset::ExperimentalLongLookahead:
            return "experimentalLongLookahead";
        case LatencyPreset::Custom:
            return "custom";
    }
    return "balanced";
}

struct JsonValue;
using JsonObject = std::map<std::string, JsonValue>;

struct JsonValue
{
    std::variant<std::nullptr_t, bool, double, std::string, JsonObject> value;

    [[nodiscard]] const JsonObject* asObject() const
    {
        return std::get_if<JsonObject>(&value);
    }

    [[nodiscard]] const std::string* asString() const
    {
        return std::get_if<std::string>(&value);
    }

    [[nodiscard]] const double* asNumber() const
    {
        return std::get_if<double>(&value);
    }

    [[nodiscard]] const bool* asBool() const
    {
        return std::get_if<bool>(&value);
    }
};

class JsonParser
{
public:
    explicit JsonParser(std::string_view text) : text_(text) {}

    JsonValue parse()
    {
        skipWhitespace();
        auto value = parseValue();
        skipWhitespace();
        if (position_ != text_.size()) {
            throw std::invalid_argument("Unexpected trailing content in preset JSON");
        }
        return value;
    }

private:
    JsonValue parseValue()
    {
        skipWhitespace();
        if (position_ >= text_.size()) {
            throw std::invalid_argument("Unexpected end of preset JSON");
        }

        const auto next = text_[position_];
        if (next == '{') {
            return JsonValue{parseObject()};
        }
        if (next == '"') {
            return JsonValue{parseString()};
        }
        if (next == 't') {
            consumeLiteral("true");
            return JsonValue{true};
        }
        if (next == 'f') {
            consumeLiteral("false");
            return JsonValue{false};
        }
        if (next == 'n') {
            consumeLiteral("null");
            return JsonValue{nullptr};
        }
        return JsonValue{parseNumber()};
    }

    JsonObject parseObject()
    {
        expect('{');
        JsonObject object;
        skipWhitespace();
        if (peek('}')) {
            expect('}');
            return object;
        }

        while (true) {
            skipWhitespace();
            auto key = parseString();
            skipWhitespace();
            expect(':');
            object.emplace(std::move(key), parseValue());
            skipWhitespace();
            if (peek('}')) {
                expect('}');
                break;
            }
            expect(',');
        }

        return object;
    }

    std::string parseString()
    {
        expect('"');
        std::string value;
        while (position_ < text_.size()) {
            const auto character = text_[position_++];
            if (character == '"') {
                return value;
            }
            if (character != '\\') {
                value.push_back(character);
                continue;
            }

            if (position_ >= text_.size()) {
                throw std::invalid_argument("Invalid escape in preset JSON string");
            }

            const auto escaped = text_[position_++];
            switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    value.push_back(escaped);
                    break;
                case 'n':
                    value.push_back('\n');
                    break;
                case 'r':
                    value.push_back('\r');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                default:
                    throw std::invalid_argument("Unsupported escape in preset JSON string");
            }
        }

        throw std::invalid_argument("Unterminated preset JSON string");
    }

    double parseNumber()
    {
        const auto start = position_;
        if (peek('-')) {
            ++position_;
        }
        while (position_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[position_]))) {
            ++position_;
        }
        if (peek('.')) {
            ++position_;
            while (position_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[position_]))) {
                ++position_;
            }
        }

        const auto token = std::string(text_.substr(start, position_ - start));
        if (token.empty() || token == "-") {
            throw std::invalid_argument("Invalid number in preset JSON");
        }

        return std::stod(token);
    }

    void skipWhitespace()
    {
        while (position_ < text_.size() &&
               std::isspace(static_cast<unsigned char>(text_[position_]))) {
            ++position_;
        }
    }

    bool peek(char expected) const
    {
        return position_ < text_.size() && text_[position_] == expected;
    }

    void expect(char expected)
    {
        if (!peek(expected)) {
            throw std::invalid_argument("Unexpected token in preset JSON");
        }
        ++position_;
    }

    void consumeLiteral(std::string_view literal)
    {
        if (text_.substr(position_, literal.size()) != literal) {
            throw std::invalid_argument("Invalid literal in preset JSON");
        }
        position_ += literal.size();
    }

    std::string_view text_;
    std::size_t position_ = 0;
};

const JsonObject* childObject(const JsonObject& object, const char* key)
{
    const auto found = object.find(key);
    if (found == object.end()) {
        return nullptr;
    }
    return found->second.asObject();
}

std::string stringValue(const JsonObject& object, const char* key, std::string fallback)
{
    const auto found = object.find(key);
    if (found == object.end()) {
        return fallback;
    }
    if (const auto* value = found->second.asString()) {
        return *value;
    }
    return fallback;
}

double numberValue(const JsonObject& object, const char* key, double fallback)
{
    const auto found = object.find(key);
    if (found == object.end()) {
        return fallback;
    }
    if (const auto* value = found->second.asNumber()) {
        return *value;
    }
    return fallback;
}

int intValue(const JsonObject& object, const char* key, int fallback)
{
    return static_cast<int>(numberValue(object, key, static_cast<double>(fallback)));
}

bool boolValue(const JsonObject& object, const char* key, bool fallback)
{
    const auto found = object.find(key);
    if (found == object.end()) {
        return fallback;
    }
    if (const auto* value = found->second.asBool()) {
        return *value;
    }
    return fallback;
}

OutputRoute parseOutputRoute(const std::string& value, OutputRoute fallback)
{
    if (value == "monitor") {
        return OutputRoute::MonitorOutput;
    }
    if (value == "projectVirtualMicrophone") {
        return OutputRoute::ProjectVirtualMicrophone;
    }
    return fallback;
}

AliasStylePreference parseAliasStylePreference(const std::string& value, AliasStylePreference fallback)
{
    if (value == "auto") {
        return AliasStylePreference::AutoDetect;
    }
    if (value == "preferRomaji") {
        return AliasStylePreference::PreferRomaji;
    }
    if (value == "preferNonAscii") {
        return AliasStylePreference::PreferNonAscii;
    }
    return fallback;
}

PitchMode parsePitchMode(const std::string& value, PitchMode fallback)
{
    if (value == "followInput") {
        return PitchMode::FollowInput;
    }
    if (value == "snapToNearestSemitone") {
        return PitchMode::SnapToNearestSemitone;
    }
    if (value == "snapToKey") {
        return PitchMode::SnapToKey;
    }
    if (value == "fixedDefault") {
        return PitchMode::FixedDefault;
    }
    return fallback;
}

ScaleType parseScaleType(const std::string& value, ScaleType fallback)
{
    if (value == "major") {
        return ScaleType::Major;
    }
    if (value == "naturalMinor") {
        return ScaleType::NaturalMinor;
    }
    if (value == "majorPentatonic") {
        return ScaleType::MajorPentatonic;
    }
    if (value == "minorPentatonic") {
        return ScaleType::MinorPentatonic;
    }
    if (value == "chromatic") {
        return ScaleType::Chromatic;
    }
    return fallback;
}

LowConfidencePitchBehavior parseLowConfidencePitchBehavior(
    const std::string& value,
    LowConfidencePitchBehavior fallback)
{
    if (value == "useRecentMean") {
        return LowConfidencePitchBehavior::UseRecentMean;
    }
    if (value == "useDefaultPitch") {
        return LowConfidencePitchBehavior::UseDefaultPitch;
    }
    return fallback;
}

LatencyPreset parseLatencyPreset(const std::string& value, LatencyPreset fallback)
{
    if (value == "lowLatency") {
        return LatencyPreset::LowLatency;
    }
    if (value == "balanced") {
        return LatencyPreset::Balanced;
    }
    if (value == "highAccuracy") {
        return LatencyPreset::HighAccuracy;
    }
    if (value == "experimentalLongLookahead") {
        return LatencyPreset::ExperimentalLongLookahead;
    }
    if (value == "custom") {
        return LatencyPreset::Custom;
    }
    return fallback;
}

} // namespace

bool AppSettingsValidation::valid() const noexcept
{
    return errors.empty();
}

AppPreset AppSettingsValidator::makeDefaultPreset()
{
    AppPreset preset;
    preset.name = "Default";
    preset.audio.outputRoute = OutputRoute::MonitorOutput;
    preset.voicebank.aliasStylePreference = AliasStylePreference::AutoDetect;
    preset.voicebank.allowMissingAliasFallback = true;
    preset.voicebank.whistleAlias = "u";
    preset.latency = LatencyBudgetCalculator::presetSettings(LatencyPreset::Balanced);
    preset.pitch = PitchTargetOptions{};
    preset.recording.optInAutoCapture = false;
    return preset;
}

AppSettingsValidation AppSettingsValidator::validate(const AppPreset& preset,
                                                     const std::string& repositoryRoot)
{
    AppSettingsValidation validation;

    if (trim(preset.name).empty()) {
        validation.errors.emplace_back("Preset name must not be empty");
    }

    addWarningIfEmpty(validation.warnings,
                      preset.audio.inputDeviceName,
                      "Input device has not been selected");
    addWarningIfEmpty(validation.warnings,
                      preset.audio.outputDeviceName,
                      "Output device has not been selected");
    addWarningIfEmpty(validation.warnings,
                      preset.voicebank.voicebankPath,
                      "Voicebank folder has not been selected");

    if (trim(preset.voicebank.whistleAlias).empty()) {
        validation.errors.emplace_back("Whistle alias must not be empty");
    }

    if (preset.pitch.defaultFrequencyHz <= 0.0) {
        validation.errors.emplace_back("Default pitch frequency must be positive");
    }

    if (preset.recording.optInAutoCapture && trim(preset.recording.privateDataFolder).empty()) {
        validation.errors.emplace_back("Auto-capture requires a private data folder");
    }

    if (!trim(repositoryRoot).empty() && !trim(preset.recording.privateDataFolder).empty() &&
        pathIsInsideDirectory(preset.recording.privateDataFolder, repositoryRoot)) {
        validation.errors.emplace_back("Private recording data folder must not be inside the Git repository");
    }

    if (trim(preset.recording.privateDataFolder).empty()) {
        validation.warnings.emplace_back("Private recording data folder has not been selected");
    }

    return validation;
}

bool AppSettingsValidator::pathIsInsideDirectory(const std::string& path,
                                                 const std::string& directory)
{
    auto child = normalizePathForWindowsComparison(trim(path));
    auto parent = normalizePathForWindowsComparison(trim(directory));

    if (child.empty() || parent.empty()) {
        return false;
    }

    if (child == parent) {
        return true;
    }

    if (parent.back() != '/') {
        parent.push_back('/');
    }

    return child.rfind(parent, 0) == 0;
}

std::string AppPresetJson::toJson(const AppPreset& preset)
{
    std::ostringstream json;
    json << std::fixed << std::setprecision(6);
    json << "{\n";
    json << "  \"schemaVersion\": " << preset.schemaVersion << ",\n";
    json << "  \"name\": \"" << escapeJsonString(preset.name) << "\",\n";
    json << "  \"audio\": {\n";
    json << "    \"inputDeviceName\": \"" << escapeJsonString(preset.audio.inputDeviceName) << "\",\n";
    json << "    \"outputDeviceName\": \"" << escapeJsonString(preset.audio.outputDeviceName) << "\",\n";
    json << "    \"outputRoute\": \"" << toJsonString(preset.audio.outputRoute) << "\"\n";
    json << "  },\n";
    json << "  \"voicebank\": {\n";
    json << "    \"voicebankPath\": \"" << escapeJsonString(preset.voicebank.voicebankPath) << "\",\n";
    json << "    \"mappingPath\": \"" << escapeJsonString(preset.voicebank.mappingPath) << "\",\n";
    json << "    \"aliasStylePreference\": \"" << toJsonString(preset.voicebank.aliasStylePreference) << "\",\n";
    json << "    \"allowMissingAliasFallback\": " << (preset.voicebank.allowMissingAliasFallback ? "true" : "false") << ",\n";
    json << "    \"whistleAlias\": \"" << escapeJsonString(preset.voicebank.whistleAlias) << "\"\n";
    json << "  },\n";
    json << "  \"pitch\": {\n";
    json << "    \"mode\": \"" << toJsonString(preset.pitch.mode) << "\",\n";
    json << "    \"scale\": \"" << toJsonString(preset.pitch.scale) << "\",\n";
    json << "    \"keyRootPitchClass\": " << preset.pitch.keyRootPitchClass << ",\n";
    json << "    \"octaveShift\": " << preset.pitch.octaveShift << ",\n";
    json << "    \"defaultFrequencyHz\": " << preset.pitch.defaultFrequencyHz << ",\n";
    json << "    \"minimumConfidence\": " << preset.pitch.minimumConfidence << ",\n";
    json << "    \"snapStrength\": " << preset.pitch.snapStrength << ",\n";
    json << "    \"lowConfidenceBehavior\": \"" << toJsonString(preset.pitch.lowConfidenceBehavior) << "\"\n";
    json << "  },\n";
    json << "  \"latency\": {\n";
    json << "    \"preset\": \"" << toJsonString(preset.latency.preset) << "\",\n";
    json << "    \"analysisWindowMs\": " << preset.latency.analysisWindowMs << ",\n";
    json << "    \"phonemeLookaheadMs\": " << preset.latency.phonemeLookaheadMs << ",\n";
    json << "    \"phonemeStabilizationMs\": " << preset.latency.phonemeStabilizationMs << ",\n";
    json << "    \"pitchSmoothingMs\": " << preset.latency.pitchSmoothingMs << ",\n";
    json << "    \"renderQueueMs\": " << preset.latency.renderQueueMs << "\n";
    json << "  },\n";
    json << "  \"recording\": {\n";
    json << "    \"privateDataFolder\": \"" << escapeJsonString(preset.recording.privateDataFolder) << "\",\n";
    json << "    \"optInAutoCapture\": " << (preset.recording.optInAutoCapture ? "true" : "false") << ",\n";
    json << "    \"recordDryInput\": " << (preset.recording.recordDryInput ? "true" : "false") << ",\n";
    json << "    \"recordSynthOutput\": " << (preset.recording.recordSynthOutput ? "true" : "false") << ",\n";
    json << "    \"recordTimelineJson\": " << (preset.recording.recordTimelineJson ? "true" : "false") << ",\n";
    json << "    \"recordPitchCsv\": " << (preset.recording.recordPitchCsv ? "true" : "false") << ",\n";
    json << "    \"recordPhonemeCsv\": " << (preset.recording.recordPhonemeCsv ? "true" : "false") << ",\n";
    json << "    \"recordAliasCsv\": " << (preset.recording.recordAliasCsv ? "true" : "false") << "\n";
    json << "  }\n";
    json << "}\n";
    return json.str();
}

AppPreset AppPresetJson::fromJson(std::string_view jsonText)
{
    auto root = JsonParser(jsonText).parse();
    const auto* rootObject = root.asObject();
    if (rootObject == nullptr) {
        throw std::invalid_argument("Preset JSON root must be an object");
    }

    auto preset = AppSettingsValidator::makeDefaultPreset();
    preset.schemaVersion = intValue(*rootObject, "schemaVersion", preset.schemaVersion);
    preset.name = stringValue(*rootObject, "name", preset.name);

    if (const auto* audio = childObject(*rootObject, "audio")) {
        preset.audio.inputDeviceName = stringValue(*audio, "inputDeviceName", preset.audio.inputDeviceName);
        preset.audio.outputDeviceName = stringValue(*audio, "outputDeviceName", preset.audio.outputDeviceName);
        preset.audio.outputRoute = parseOutputRoute(
            stringValue(*audio, "outputRoute", toJsonString(preset.audio.outputRoute)),
            preset.audio.outputRoute);
    }

    if (const auto* voicebank = childObject(*rootObject, "voicebank")) {
        preset.voicebank.voicebankPath = stringValue(*voicebank, "voicebankPath", preset.voicebank.voicebankPath);
        preset.voicebank.mappingPath = stringValue(*voicebank, "mappingPath", preset.voicebank.mappingPath);
        preset.voicebank.aliasStylePreference = parseAliasStylePreference(
            stringValue(*voicebank,
                        "aliasStylePreference",
                        toJsonString(preset.voicebank.aliasStylePreference)),
            preset.voicebank.aliasStylePreference);
        preset.voicebank.allowMissingAliasFallback =
            boolValue(*voicebank, "allowMissingAliasFallback", preset.voicebank.allowMissingAliasFallback);
        preset.voicebank.whistleAlias = stringValue(*voicebank, "whistleAlias", preset.voicebank.whistleAlias);
    }

    if (const auto* pitch = childObject(*rootObject, "pitch")) {
        preset.pitch.mode = parsePitchMode(stringValue(*pitch, "mode", toJsonString(preset.pitch.mode)),
                                           preset.pitch.mode);
        preset.pitch.scale = parseScaleType(stringValue(*pitch, "scale", toJsonString(preset.pitch.scale)),
                                            preset.pitch.scale);
        preset.pitch.keyRootPitchClass = intValue(*pitch, "keyRootPitchClass", preset.pitch.keyRootPitchClass);
        preset.pitch.octaveShift = intValue(*pitch, "octaveShift", preset.pitch.octaveShift);
        preset.pitch.defaultFrequencyHz = numberValue(*pitch, "defaultFrequencyHz", preset.pitch.defaultFrequencyHz);
        preset.pitch.minimumConfidence = numberValue(*pitch, "minimumConfidence", preset.pitch.minimumConfidence);
        preset.pitch.snapStrength = numberValue(*pitch, "snapStrength", preset.pitch.snapStrength);
        preset.pitch.lowConfidenceBehavior = parseLowConfidencePitchBehavior(
            stringValue(*pitch, "lowConfidenceBehavior", toJsonString(preset.pitch.lowConfidenceBehavior)),
            preset.pitch.lowConfidenceBehavior);
    }

    if (const auto* latency = childObject(*rootObject, "latency")) {
        preset.latency.preset = parseLatencyPreset(stringValue(*latency,
                                                               "preset",
                                                               toJsonString(preset.latency.preset)),
                                                   preset.latency.preset);
        preset.latency.analysisWindowMs = numberValue(*latency, "analysisWindowMs", preset.latency.analysisWindowMs);
        preset.latency.phonemeLookaheadMs = numberValue(*latency, "phonemeLookaheadMs", preset.latency.phonemeLookaheadMs);
        preset.latency.phonemeStabilizationMs = numberValue(*latency,
                                                            "phonemeStabilizationMs",
                                                            preset.latency.phonemeStabilizationMs);
        preset.latency.pitchSmoothingMs = numberValue(*latency, "pitchSmoothingMs", preset.latency.pitchSmoothingMs);
        preset.latency.renderQueueMs = numberValue(*latency, "renderQueueMs", preset.latency.renderQueueMs);
    }

    if (const auto* recording = childObject(*rootObject, "recording")) {
        preset.recording.privateDataFolder =
            stringValue(*recording, "privateDataFolder", preset.recording.privateDataFolder);
        preset.recording.optInAutoCapture =
            boolValue(*recording, "optInAutoCapture", preset.recording.optInAutoCapture);
        preset.recording.recordDryInput = boolValue(*recording, "recordDryInput", preset.recording.recordDryInput);
        preset.recording.recordSynthOutput =
            boolValue(*recording, "recordSynthOutput", preset.recording.recordSynthOutput);
        preset.recording.recordTimelineJson =
            boolValue(*recording, "recordTimelineJson", preset.recording.recordTimelineJson);
        preset.recording.recordPitchCsv = boolValue(*recording, "recordPitchCsv", preset.recording.recordPitchCsv);
        preset.recording.recordPhonemeCsv =
            boolValue(*recording, "recordPhonemeCsv", preset.recording.recordPhonemeCsv);
        preset.recording.recordAliasCsv = boolValue(*recording, "recordAliasCsv", preset.recording.recordAliasCsv);
    }

    return preset;
}

} // namespace Voice2VocalSynth
