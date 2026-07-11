#include "Voice2VocalSynth/PhonemeEvaluation.h"
#include "Voice2VocalSynth/PhonemeFallbackMapper.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <variant>

namespace Voice2VocalSynth
{
namespace
{

double overlapSeconds(const PhonemeFrame& a, const PhonemeFrame& b)
{
    return std::max(0.0,
                    std::min(a.estimatedEndSeconds, b.estimatedEndSeconds) -
                        std::max(a.estimatedOnsetSeconds, b.estimatedOnsetSeconds));
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

struct JsonValue;
using JsonObject = std::map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;

struct JsonValue
{
    std::variant<std::nullptr_t, bool, double, std::string, JsonObject, JsonArray> value;

    [[nodiscard]] const JsonObject* asObject() const { return std::get_if<JsonObject>(&value); }
    [[nodiscard]] const JsonArray* asArray() const { return std::get_if<JsonArray>(&value); }
    [[nodiscard]] const std::string* asString() const { return std::get_if<std::string>(&value); }
    [[nodiscard]] const double* asNumber() const { return std::get_if<double>(&value); }
    [[nodiscard]] const bool* asBool() const { return std::get_if<bool>(&value); }
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
            throw std::invalid_argument("Unexpected trailing content");
        }
        return value;
    }

private:
    JsonValue parseValue()
    {
        skipWhitespace();
        if (position_ >= text_.size()) {
            throw std::invalid_argument("Unexpected end of JSON");
        }

        const char next = text_[position_];
        if (next == '{') {
            return JsonValue{parseObject()};
        }
        if (next == '[') {
            return JsonValue{parseArray()};
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
                return object;
            }
            expect(',');
        }
    }

    JsonArray parseArray()
    {
        expect('[');
        JsonArray array;
        skipWhitespace();
        if (peek(']')) {
            expect(']');
            return array;
        }

        while (true) {
            array.push_back(parseValue());
            skipWhitespace();
            if (peek(']')) {
                expect(']');
                return array;
            }
            expect(',');
        }
    }

    std::string parseString()
    {
        expect('"');
        std::string value;
        while (position_ < text_.size()) {
            const char c = text_[position_++];
            if (c == '"') {
                return value;
            }
            if (c != '\\') {
                value.push_back(c);
                continue;
            }
            if (position_ >= text_.size()) {
                throw std::invalid_argument("Invalid string escape");
            }

            const char escaped = text_[position_++];
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
                    throw std::invalid_argument("Unsupported string escape");
            }
        }
        throw std::invalid_argument("Unterminated string");
    }

    double parseNumber()
    {
        const auto start = position_;
        if (peek('-')) {
            ++position_;
        }

        const auto integerStart = position_;
        while (position_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[position_]))) {
            ++position_;
        }
        if (position_ == integerStart) {
            throw std::invalid_argument("Invalid number");
        }

        if (peek('.')) {
            ++position_;
            const auto fractionStart = position_;
            while (position_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[position_]))) {
                ++position_;
            }
            if (position_ == fractionStart) {
                throw std::invalid_argument("Invalid fractional number");
            }
        }

        if (peek('e') || peek('E')) {
            ++position_;
            if (peek('+') || peek('-')) {
                ++position_;
            }
            const auto exponentStart = position_;
            while (position_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[position_]))) {
                ++position_;
            }
            if (position_ == exponentStart) {
                throw std::invalid_argument("Invalid exponent");
            }
        }

        return std::stod(std::string(text_.substr(start, position_ - start)));
    }

    void skipWhitespace()
    {
        while (position_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[position_]))) {
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
            throw std::invalid_argument("Unexpected JSON token");
        }
        ++position_;
    }

    void consumeLiteral(std::string_view literal)
    {
        if (text_.substr(position_, literal.size()) != literal) {
            throw std::invalid_argument("Invalid JSON literal");
        }
        position_ += literal.size();
    }

    std::string_view text_;
    std::size_t position_ = 0;
};

std::string stringValue(const JsonObject& object, const char* key, std::string fallback = {})
{
    const auto it = object.find(key);
    if (it == object.end()) {
        return fallback;
    }
    if (const auto* value = it->second.asString()) {
        return *value;
    }
    return fallback;
}

double numberValue(const JsonObject& object, const char* key, double fallback = 0.0)
{
    const auto it = object.find(key);
    if (it == object.end()) {
        return fallback;
    }
    if (const auto* value = it->second.asNumber()) {
        return *value;
    }
    return fallback;
}

bool boolValue(const JsonObject& object, const char* key, bool fallback = false)
{
    const auto it = object.find(key);
    if (it == object.end()) {
        return fallback;
    }
    if (const auto* value = it->second.asBool()) {
        return *value;
    }
    return fallback;
}

} // namespace

bool phonemeFrameIsConsonant(const PhonemeFrame& frame)
{
    if (frame.isConsonant) {
        return true;
    }
    if (frame.isVowel) {
        return false;
    }
    return !PhonemeFallbackMapper::isArpabetVowel(frame.arpabet);
}

double percentileSeconds(const std::vector<double>& valuesSeconds, double quantile)
{
    if (valuesSeconds.empty()) {
        return 0.0;
    }
    auto sorted = valuesSeconds;
    std::sort(sorted.begin(), sorted.end());
    const double clamped = std::clamp(quantile, 0.0, 1.0);
    const double position = clamped * static_cast<double>(sorted.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto upper = static_cast<std::size_t>(std::ceil(position));
    if (lower == upper) {
        return sorted[lower];
    }
    const double fraction = position - static_cast<double>(lower);
    return sorted[lower] * (1.0 - fraction) + sorted[upper] * fraction;
}

PhonemeEvaluationMetrics evaluatePhonemeFrames(
    const std::vector<PhonemeFrame>& reference,
    const std::vector<PhonemeFrame>& prediction,
    const PhonemeEvaluationOptions& options)
{
    PhonemeEvaluationMetrics metrics;
    metrics.referenceCount = reference.size();
    metrics.predictionCount = prediction.size();

    std::vector<bool> predictionUsed(prediction.size(), false);
    double onsetErrorSumSeconds = 0.0;
    double endErrorSumSeconds = 0.0;
    double durationErrorSumSeconds = 0.0;
    std::vector<double> matchedOnsetErrorsSeconds;
    std::vector<double> matchedEndErrorsSeconds;
    std::vector<double> matchedDurationErrorsSeconds;
    std::vector<std::size_t> missedReferenceIndices;

    for (std::size_t referenceIndex = 0; referenceIndex < reference.size(); ++referenceIndex) {
        const auto& ref = reference[referenceIndex];
        if (phonemeFrameIsConsonant(ref)) {
            ++metrics.referenceConsonantCount;
        }

        std::size_t bestIndex = prediction.size();
        double bestOverlap = 0.0;
        double bestOnsetError = std::numeric_limits<double>::infinity();

        for (std::size_t index = 0; index < prediction.size(); ++index) {
            if (predictionUsed[index] || prediction[index].arpabet != ref.arpabet) {
                continue;
            }

            const double overlap = overlapSeconds(ref, prediction[index]);
            const double onsetError = std::abs(prediction[index].estimatedOnsetSeconds -
                                               ref.estimatedOnsetSeconds);
            if (overlap >= options.minOverlapSeconds &&
                onsetError <= options.maxOnsetErrorSeconds &&
                (overlap > bestOverlap || (overlap == bestOverlap && onsetError < bestOnsetError))) {
                bestIndex = index;
                bestOverlap = overlap;
                bestOnsetError = onsetError;
            }
        }

        if (bestIndex == prediction.size()) {
            ++metrics.missedCount;
            missedReferenceIndices.push_back(referenceIndex);
            if (phonemeFrameIsConsonant(ref)) {
                ++metrics.missedConsonantCount;
            }
            continue;
        }

        predictionUsed[bestIndex] = true;
        ++metrics.matchedCount;
        onsetErrorSumSeconds += bestOnsetError;
        matchedOnsetErrorsSeconds.push_back(bestOnsetError);
        const double endError = std::abs(prediction[bestIndex].estimatedEndSeconds -
                                         ref.estimatedEndSeconds);
        const double referenceDuration = ref.estimatedEndSeconds - ref.estimatedOnsetSeconds;
        const double predictionDuration = prediction[bestIndex].estimatedEndSeconds -
                                          prediction[bestIndex].estimatedOnsetSeconds;
        const double durationError = std::abs(predictionDuration - referenceDuration);
        endErrorSumSeconds += endError;
        durationErrorSumSeconds += durationError;
        matchedEndErrorsSeconds.push_back(endError);
        matchedDurationErrorsSeconds.push_back(durationError);
    }

    metrics.falsePositiveCount = static_cast<std::size_t>(
        std::count(predictionUsed.begin(), predictionUsed.end(), false));
    metrics.substitutionOrTimingErrorCount = metrics.missedCount + metrics.falsePositiveCount;

    // Summarize likely substitutions separately from exact-label scoring. Pair
    // unmatched segments by strongest overlap without changing precision/recall.
    std::vector<bool> confusionPredictionUsed = predictionUsed;
    for (const auto referenceIndex : missedReferenceIndices) {
        const auto& ref = reference[referenceIndex];
        std::size_t bestIndex = prediction.size();
        double bestOverlap = 0.0;
        double bestOnset = std::numeric_limits<double>::infinity();
        for (std::size_t index = 0; index < prediction.size(); ++index) {
            if (confusionPredictionUsed[index] || prediction[index].arpabet == ref.arpabet) {
                continue;
            }
            const double overlap = overlapSeconds(ref, prediction[index]);
            const double onset = std::abs(prediction[index].estimatedOnsetSeconds -
                                          ref.estimatedOnsetSeconds);
            if ((overlap > 0.0 || onset <= options.maxOnsetErrorSeconds) &&
                (overlap > bestOverlap || (overlap == bestOverlap && onset < bestOnset))) {
                bestIndex = index;
                bestOverlap = overlap;
                bestOnset = onset;
            }
        }
        if (bestIndex != prediction.size()) {
            confusionPredictionUsed[bestIndex] = true;
            ++metrics.confusionCounts[ref.arpabet + "->" + prediction[bestIndex].arpabet];
        }
    }

    if (metrics.predictionCount > 0) {
        metrics.precision = static_cast<double>(metrics.matchedCount) /
                            static_cast<double>(metrics.predictionCount);
    }
    if (metrics.referenceCount > 0) {
        metrics.recall = static_cast<double>(metrics.matchedCount) /
                         static_cast<double>(metrics.referenceCount);
        metrics.missedRate = static_cast<double>(metrics.missedCount) /
                             static_cast<double>(metrics.referenceCount);
    }
    if (metrics.precision + metrics.recall > 0.0) {
        metrics.f1 = 2.0 * metrics.precision * metrics.recall /
                     (metrics.precision + metrics.recall);
    }
    if (metrics.matchedCount > 0) {
        metrics.meanAbsoluteOnsetErrorMs =
            (onsetErrorSumSeconds * 1000.0) / static_cast<double>(metrics.matchedCount);
        metrics.p95OnsetErrorMs =
            percentileSeconds(matchedOnsetErrorsSeconds, 0.95) * 1000.0;
        metrics.meanAbsoluteEndErrorMs =
            (endErrorSumSeconds * 1000.0) / static_cast<double>(metrics.matchedCount);
        metrics.p95EndErrorMs =
            percentileSeconds(matchedEndErrorsSeconds, 0.95) * 1000.0;
        metrics.meanAbsoluteDurationErrorMs =
            (durationErrorSumSeconds * 1000.0) / static_cast<double>(metrics.matchedCount);
        metrics.p95DurationErrorMs =
            percentileSeconds(matchedDurationErrorsSeconds, 0.95) * 1000.0;
    }
    if (metrics.predictionCount > 0) {
        metrics.falsePositiveRate = static_cast<double>(metrics.falsePositiveCount) /
                                    static_cast<double>(metrics.predictionCount);
    }
    if (metrics.referenceConsonantCount > 0) {
        metrics.missedConsonantRate = static_cast<double>(metrics.missedConsonantCount) /
                                      static_cast<double>(metrics.referenceConsonantCount);
    }

    return metrics;
}

PhonemeFrameJsonLoadResult parsePhonemeFrameLabelsJson(std::string_view json)
{
    PhonemeFrameJsonLoadResult result;
    try {
        const auto root = JsonParser(json).parse();
        const auto* array = root.asArray();
        if (array == nullptr) {
            result.error = "Phoneme label JSON root must be an array";
            return result;
        }

        for (const auto& value : *array) {
            const auto* object = value.asObject();
            if (object == nullptr) {
                result.error = "Each phoneme label entry must be an object";
                result.frames.clear();
                return result;
            }

            PhonemeFrame frame;
            frame.arpabet = stringValue(*object, "arpabet");
            frame.confidence = static_cast<float>(numberValue(*object, "confidence", 1.0));
            frame.estimatedOnsetSeconds = numberValue(*object,
                                                      "estimatedOnsetSeconds",
                                                      numberValue(*object, "start", 0.0));
            frame.estimatedEndSeconds = numberValue(*object,
                                                    "estimatedEndSeconds",
                                                    numberValue(*object, "end", frame.estimatedOnsetSeconds));
            frame.isVoiced = boolValue(*object, "isVoiced", false);
            frame.isConsonant = boolValue(*object, "isConsonant", false);
            frame.isVowel = boolValue(*object, "isVowel", false);
            if (frame.arpabet.empty()) {
                result.error = "Phoneme label entry is missing arpabet";
                result.frames.clear();
                return result;
            }
            result.frames.push_back(std::move(frame));
        }

        result.ok = true;
        return result;
    } catch (const std::exception& e) {
        result.error = e.what();
        result.frames.clear();
        return result;
    }
}

PhonemeFrameJsonLoadResult loadPhonemeFrameLabelsJson(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        PhonemeFrameJsonLoadResult result;
        result.error = "Unable to open phoneme label JSON: " + path.string();
        return result;
    }

    std::ostringstream contents;
    contents << input.rdbuf();
    return parsePhonemeFrameLabelsJson(contents.str());
}

std::string phonemeEvaluationMetricsToJson(const PhonemeEvaluationMetrics& metrics)
{
    std::ostringstream json;
    json << std::fixed;
    json.precision(6);
    json << "{\n";
    json << "  \"schemaVersion\": 1,\n";
    json << "  \"referenceCount\": " << metrics.referenceCount << ",\n";
    json << "  \"predictionCount\": " << metrics.predictionCount << ",\n";
    json << "  \"matchedCount\": " << metrics.matchedCount << ",\n";
    json << "  \"substitutionOrTimingErrorCount\": " << metrics.substitutionOrTimingErrorCount << ",\n";
    json << "  \"missedCount\": " << metrics.missedCount << ",\n";
    json << "  \"falsePositiveCount\": " << metrics.falsePositiveCount << ",\n";
    json << "  \"referenceConsonantCount\": " << metrics.referenceConsonantCount << ",\n";
    json << "  \"missedConsonantCount\": " << metrics.missedConsonantCount << ",\n";
    json << "  \"precision\": " << metrics.precision << ",\n";
    json << "  \"recall\": " << metrics.recall << ",\n";
    json << "  \"f1\": " << metrics.f1 << ",\n";
    json << "  \"falsePositiveRate\": " << metrics.falsePositiveRate << ",\n";
    json << "  \"missedRate\": " << metrics.missedRate << ",\n";
    json << "  \"missedConsonantRate\": " << metrics.missedConsonantRate << ",\n";
    json << "  \"meanAbsoluteOnsetErrorMs\": " << metrics.meanAbsoluteOnsetErrorMs << ",\n";
    json << "  \"p95OnsetErrorMs\": " << metrics.p95OnsetErrorMs << ",\n";
    json << "  \"meanAbsoluteEndErrorMs\": " << metrics.meanAbsoluteEndErrorMs << ",\n";
    json << "  \"p95EndErrorMs\": " << metrics.p95EndErrorMs << ",\n";
    json << "  \"meanAbsoluteDurationErrorMs\": " << metrics.meanAbsoluteDurationErrorMs << ",\n";
    json << "  \"p95DurationErrorMs\": " << metrics.p95DurationErrorMs << ",\n";
    json << "  \"confusionCounts\": {";
    std::size_t confusionIndex = 0;
    for (const auto& [pair, count] : metrics.confusionCounts) {
        if (confusionIndex++ > 0) {
            json << ',';
        }
        json << "\n    \"" << escapeJsonString(pair) << "\": " << count;
    }
    if (!metrics.confusionCounts.empty()) {
        json << '\n' << "  ";
    }
    json << "}\n";
    json << "}\n";
    return json.str();
}

} // namespace Voice2VocalSynth
