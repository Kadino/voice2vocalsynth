#include "Voice2VocalSynth/PhonemeMappingConfigLoader.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <variant>

namespace Voice2VocalSynth
{
namespace
{

struct JsonValue;

using JsonObject = std::map<std::string, JsonValue>;

struct JsonValue : std::variant<std::nullptr_t,
                                bool,
                                double,
                                std::string,
                                JsonObject,
                                std::vector<std::string>>
{
    using variant::variant;
};

class JsonReader
{
public:
    explicit JsonReader(std::string_view text)
        : text_(text)
    {
    }

    JsonObject parseObject()
    {
        skipWhitespace();
        if (peek() != '{') {
            throw std::invalid_argument("Expected JSON object");
        }
        consume();
        JsonObject object;
        skipWhitespace();
        if (peek() == '}') {
            consume();
            return object;
        }
        for (;;) {
            auto key = parseString();
            skipWhitespace();
            if (consume() != ':') {
                throw std::invalid_argument("Expected ':' after object key");
            }
            object.emplace(std::move(key), parseValue());
            skipWhitespace();
            const char delim = consume();
            if (delim == '}') {
                break;
            }
            if (delim != ',') {
                throw std::invalid_argument("Expected ',' or '}' in object");
            }
            skipWhitespace();
        }
        return object;
    }

private:
    std::string_view text_;
    std::size_t pos_ = 0;

    char peek() const
    {
        return pos_ < text_.size() ? text_[pos_] : '\0';
    }

    char consume()
    {
        return pos_ < text_.size() ? text_[pos_++] : '\0';
    }

    void skipWhitespace()
    {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
    }

    std::string parseString()
    {
        skipWhitespace();
        if (consume() != '"') {
            throw std::invalid_argument("Expected string");
        }
        std::string out;
        while (pos_ < text_.size()) {
            const char c = consume();
            if (c == '"') {
                return out;
            }
            if (c == '\\') {
                const char e = consume();
                switch (e) {
                    case '"':
                    case '\\':
                    case '/':
                        out.push_back(e);
                        break;
                    case 'n':
                        out.push_back('\n');
                        break;
                    case 'r':
                        out.push_back('\r');
                        break;
                    case 't':
                        out.push_back('\t');
                        break;
                    default:
                        throw std::invalid_argument("Unsupported JSON escape");
                }
                continue;
            }
            out.push_back(c);
        }
        throw std::invalid_argument("Unterminated JSON string");
    }

    double parseNumber()
    {
        skipWhitespace();
        const std::size_t start = pos_;
        if (peek() == '-') {
            consume();
        }
        while (std::isdigit(static_cast<unsigned char>(peek()))) {
            consume();
        }
        if (peek() == '.') {
            consume();
            while (std::isdigit(static_cast<unsigned char>(peek()))) {
                consume();
            }
        }
        const auto slice = text_.substr(start, pos_ - start);
        return std::stod(std::string(slice));
    }

    std::vector<std::string> parseStringArray()
    {
        skipWhitespace();
        if (consume() != '[') {
            throw std::invalid_argument("Expected array");
        }
        std::vector<std::string> values;
        skipWhitespace();
        if (peek() == ']') {
            consume();
            return values;
        }
        for (;;) {
            values.push_back(parseString());
            skipWhitespace();
            const char delim = consume();
            if (delim == ']') {
                break;
            }
            if (delim != ',') {
                throw std::invalid_argument("Expected ',' or ']' in array");
            }
            skipWhitespace();
        }
        return values;
    }

    JsonValue parseValue()
    {
        skipWhitespace();
        const char c = peek();
        if (c == '"') {
            return parseString();
        }
        if (c == '{') {
            JsonReader nested(text_.substr(pos_));
            auto obj = nested.parseObject();
            pos_ += nested.pos_;
            return obj;
        }
        if (c == '[') {
            return parseStringArray();
        }
        if (std::isalpha(static_cast<unsigned char>(c))) {
            const std::size_t start = pos_;
            while (pos_ < text_.size() &&
                   std::isalpha(static_cast<unsigned char>(text_[pos_]))) {
                ++pos_;
            }
            const auto word = text_.substr(start, pos_ - start);
            if (word == "true") {
                return true;
            }
            if (word == "false") {
                return false;
            }
            if (word == "null") {
                return nullptr;
            }
            throw std::invalid_argument("Invalid JSON literal");
        }
        return parseNumber();
    }
};

const JsonObject* asObject(const JsonObject::mapped_type& value)
{
    return std::get_if<JsonObject>(&value);
}

const std::string* asString(const JsonObject::mapped_type& value)
{
    return std::get_if<std::string>(&value);
}

const std::vector<std::string>* asStringArray(const JsonObject::mapped_type& value)
{
    return std::get_if<std::vector<std::string>>(&value);
}

const bool* asBool(const JsonObject::mapped_type& value)
{
    return std::get_if<bool>(&value);
}

const double* asNumber(const JsonObject::mapped_type& value)
{
    return std::get_if<double>(&value);
}

void mergeStringMap(std::unordered_map<std::string, std::string>& target,
                    const JsonObject& source,
                    std::vector<std::string>& warnings,
                    const char* section)
{
    for (const auto& [key, value] : source) {
        const auto* mapped = asString(value);
        if (mapped == nullptr) {
            warnings.push_back(std::string(section) + ": ignored non-string entry for key " + key);
            continue;
        }
        const auto normalized = PhonemeFallbackMapper::normalizeArpabet(key);
        if (normalized.empty()) {
            continue;
        }
        target[normalized] = *mapped;
    }
}

void mergeStringArrayMap(std::unordered_map<std::string, std::vector<std::string>>& target,
                         const JsonObject& source,
                         std::vector<std::string>& warnings,
                         const char* section)
{
    for (const auto& [key, value] : source) {
        const auto* items = asStringArray(value);
        if (items == nullptr) {
            warnings.push_back(std::string(section) + ": ignored non-array entry for key " + key);
            continue;
        }
        const auto normalized = PhonemeFallbackMapper::normalizeArpabet(key);
        if (normalized.empty()) {
            continue;
        }
        target[normalized] = *items;
    }
}

PhonemeFallbackOptions parseOptionsObject(const JsonObject& root, std::vector<std::string>& warnings)
{
    PhonemeFallbackOptions options = PhonemeFallbackMapper::makeDefaultOptions();

    if (const auto it = root.find("enablePartialCvcFallback"); it != root.end()) {
        if (const auto* b = asBool(it->second)) {
            options.enablePartialCvcFallback = *b;
        }
    }
    if (const auto it = root.find("defaultFinalConsonantVowel"); it != root.end()) {
        if (const auto* s = asString(it->second)) {
            options.defaultFinalConsonantVowel = *s;
        }
    }
    if (const auto it = root.find("partialFinalVowelTailGain"); it != root.end()) {
        if (const auto* n = asNumber(it->second)) {
            options.partialFinalVowelTailGain = static_cast<float>(*n);
        }
    }
    if (const auto it = root.find("partialFinalMaxDurationMs"); it != root.end()) {
        if (const auto* n = asNumber(it->second)) {
            options.partialFinalMaxDurationMs = *n;
        }
    }

    if (const auto it = root.find("vowelFallbacks"); it != root.end()) {
        if (const auto* obj = asObject(it->second)) {
            mergeStringMap(options.vowelSubstitutions, *obj, warnings, "vowelFallbacks");
        }
    }
    if (const auto it = root.find("consonantFallbacks"); it != root.end()) {
        if (const auto* obj = asObject(it->second)) {
            mergeStringMap(options.consonantSubstitutions, *obj, warnings, "consonantFallbacks");
        }
    }
    if (const auto it = root.find("consonantAlternativeFallbacks"); it != root.end()) {
        if (const auto* obj = asObject(it->second)) {
            mergeStringArrayMap(options.consonantFallbackCandidates, *obj, warnings,
                                "consonantAlternativeFallbacks");
        }
    }
    if (const auto it = root.find("finalConsonantVowelOverrides"); it != root.end()) {
        if (const auto* obj = asObject(it->second)) {
            mergeStringMap(options.finalConsonantVowelOverrides, *obj, warnings,
                           "finalConsonantVowelOverrides");
        }
    }
    if (const auto it = root.find("finalConsonantAliasOverrides"); it != root.end()) {
        if (const auto* obj = asObject(it->second)) {
            mergeStringMap(options.finalConsonantAliasOverrides, *obj, warnings,
                           "finalConsonantAliasOverrides");
        }
    }

    return options;
}

} // namespace

PhonemeFallbackOptions PhonemeMappingConfigLoader::mergeWithDefaults(
    const PhonemeFallbackOptions& from_file)
{
    auto merged = PhonemeFallbackMapper::makeDefaultOptions();

    auto overlay = [](auto& target, const auto& overlay_map) {
        for (const auto& [key, value] : overlay_map) {
            target[key] = value;
        }
    };

    overlay(merged.vowelSubstitutions, from_file.vowelSubstitutions);
    overlay(merged.consonantSubstitutions, from_file.consonantSubstitutions);
    overlay(merged.consonantFallbackCandidates, from_file.consonantFallbackCandidates);
    overlay(merged.finalConsonantVowelOverrides, from_file.finalConsonantVowelOverrides);
    overlay(merged.finalConsonantAliasOverrides, from_file.finalConsonantAliasOverrides);

    if (!from_file.vowelSubstitutions.empty() || !from_file.consonantSubstitutions.empty() ||
        !from_file.consonantFallbackCandidates.empty() ||
        !from_file.finalConsonantVowelOverrides.empty() ||
        !from_file.finalConsonantAliasOverrides.empty()) {
        merged.enablePartialCvcFallback = from_file.enablePartialCvcFallback;
        if (!from_file.defaultFinalConsonantVowel.empty()) {
            merged.defaultFinalConsonantVowel = from_file.defaultFinalConsonantVowel;
        }
        merged.partialFinalVowelTailGain = from_file.partialFinalVowelTailGain;
        merged.partialFinalMaxDurationMs = from_file.partialFinalMaxDurationMs;
    }

    return merged;
}

std::string PhonemeMappingConfigLoader::defaultConfigJson()
{
    std::ostringstream json;
    json << "{\n";
    json << "  \"phonemeSet\": \"arpabet\",\n";
    json << "  \"enablePartialCvcFallback\": true,\n";
    json << "  \"defaultFinalConsonantVowel\": \"u\",\n";
    json << "  \"partialFinalVowelTailGain\": 0.15,\n";
    json << "  \"partialFinalMaxDurationMs\": 90,\n";
    json << "  \"vowelFallbacks\": {\n";
    json << "    \"AA\": \"a\", \"AE\": \"a\", \"AH\": \"a\", \"AO\": \"o\", \"AW\": \"a\",\n";
    json << "    \"AY\": \"a\", \"EH\": \"e\", \"ER\": \"a\", \"EY\": \"e\", \"IH\": \"i\",\n";
    json << "    \"IY\": \"i\", \"OW\": \"o\", \"OY\": \"o\", \"UH\": \"u\", \"UW\": \"u\"\n";
    json << "  },\n";
    json << "  \"consonantFallbacks\": {\n";
    json << "    \"B\": \"b\", \"CH\": \"ch\", \"D\": \"d\", \"DH\": \"z\", \"F\": \"f\",\n";
    json << "    \"G\": \"g\", \"HH\": \"h\", \"JH\": \"j\", \"K\": \"k\", \"L\": \"r\",\n";
    json << "    \"M\": \"m\", \"N\": \"n\", \"NG\": \"n\", \"P\": \"p\", \"R\": \"r\",\n";
    json << "    \"S\": \"s\", \"SH\": \"sh\", \"T\": \"t\", \"TH\": \"s\", \"V\": \"b\",\n";
    json << "    \"W\": \"w\", \"Y\": \"y\", \"Z\": \"z\", \"ZH\": \"j\"\n";
    json << "  },\n";
    json << "  \"consonantAlternativeFallbacks\": {\n";
    json << "    \"DH\": [\"d\"], \"F\": [\"h\"], \"TH\": [\"t\"], \"V\": [\"f\", \"b\"], \"ZH\": [\"j\"]\n";
    json << "  },\n";
    json << "  \"finalConsonantAliasOverrides\": { \"N\": \"n\", \"NG\": \"n\" },\n";
    json << "  \"finalConsonantVowelOverrides\": { \"D\": \"o\", \"T\": \"o\" }\n";
    json << "}\n";
    return json.str();
}

std::filesystem::path PhonemeMappingConfigLoader::defaultUserConfigPath()
{
    return std::filesystem::path("Voice2VocalSynth") / "phoneme_to_japanese.json";
}

std::filesystem::path PhonemeMappingConfigLoader::repositoryTemplatePath()
{
#ifdef VOICE2VOCALSYNTH_REPOSITORY_ROOT
    return std::filesystem::path(VOICE2VOCALSYNTH_REPOSITORY_ROOT) / "config" / "phoneme_to_japanese.json";
#else
    return std::filesystem::path("config") / "phoneme_to_japanese.json";
#endif
}

PhonemeMappingLoadResult PhonemeMappingConfigLoader::loadFromJson(std::string_view json)
{
    PhonemeMappingLoadResult result;
    result.options = PhonemeFallbackMapper::makeDefaultOptions();

    try {
        JsonReader reader(json);
        const auto root = reader.parseObject();
        result.options = parseOptionsObject(root, result.warnings);
        result.ok = true;
        result.used_file = true;
    } catch (const std::exception& ex) {
        result.ok = false;
        result.error = ex.what();
        result.options = PhonemeFallbackMapper::makeDefaultOptions();
    }

    return result;
}

PhonemeMappingLoadResult PhonemeMappingConfigLoader::loadFromFile(const std::filesystem::path& path)
{
    PhonemeMappingLoadResult result;
    result.options = PhonemeFallbackMapper::makeDefaultOptions();

    if (path.empty()) {
        result.ok = false;
        result.error = "mapping path is empty";
        return result;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        result.ok = false;
        result.error = "could not open mapping file: " + path.string();
        return result;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    result = loadFromJson(buffer.str());
    if (!result.ok) {
        result.error = "mapping file \"" + path.string() + "\": " + result.error;
        result.options = PhonemeFallbackMapper::makeDefaultOptions();
        return result;
    }

    result.used_file = true;
    return result;
}

PhonemeMappingLoadResult PhonemeMappingConfigLoader::loadEffective(
    const std::optional<std::filesystem::path>& explicit_path)
{
    PhonemeMappingLoadResult result;
    result.options = PhonemeFallbackMapper::makeDefaultOptions();

    std::vector<std::filesystem::path> candidates;
    if (explicit_path && !explicit_path->empty()) {
        candidates.push_back(*explicit_path);
    }
    candidates.push_back(defaultUserConfigPath());
    candidates.push_back(repositoryTemplatePath());

    for (const auto& candidate : candidates) {
        if (candidate.empty() || !std::filesystem::exists(candidate)) {
            continue;
        }
        auto loaded = loadFromFile(candidate);
        if (loaded.ok) {
            loaded.warnings.insert(loaded.warnings.begin(),
                                   "Loaded phoneme mapping from " + candidate.string());
            return loaded;
        }
        result.warnings.push_back(loaded.error);
    }

    result.ok = true;
    result.options = PhonemeFallbackMapper::makeDefaultOptions();
    result.warnings.push_back(
        "No phoneme mapping file found; using built-in defaults (see config/phoneme_to_japanese.json)");
    return result;
}

} // namespace Voice2VocalSynth
