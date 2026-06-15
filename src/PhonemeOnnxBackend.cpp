#include "Voice2VocalSynth/PhonemeBackend.h"
#include "Voice2VocalSynth/PhonemeOnnxRunner.h"

#include <algorithm>
#include <chrono>
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

[[nodiscard]] PhonemeBackendInputKind parseInputKind(std::string_view text)
{
    if (text == "flatTensor") {
        return PhonemeBackendInputKind::FlatTensor;
    }
    return PhonemeBackendInputKind::MonoPcm;
}

[[nodiscard]] std::filesystem::path defaultConfigPathForModel(const std::filesystem::path& modelPath)
{
    const auto stem = modelPath.stem().string();
    const auto parent = modelPath.parent_path();
    return parent / (stem + ".phoneme.json");
}

[[nodiscard]] std::size_t samplesForWindow(double sampleRateHz, double windowMs)
{
    return static_cast<std::size_t>(
        std::max(1.0, static_cast<double>(std::llround(sampleRateHz * windowMs / 1000.0))));
}

[[nodiscard]] float normalizeConfidence(double raw, double minValue, double maxValue)
{
    if (maxValue <= minValue) {
        return static_cast<float>(std::clamp(raw, 0.0, 1.0));
    }
    const double normalized = (raw - minValue) / (maxValue - minValue);
    return static_cast<float>(std::clamp(normalized, 0.0, 1.0));
}

} // namespace

PhonemeOnnxModelConfigLoadResult loadPhonemeOnnxModelConfigJson(const std::filesystem::path& path)
{
    PhonemeOnnxModelConfigLoadResult result;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        result.error = "Unable to open phoneme ONNX config JSON: " + path.string();
        return result;
    }

    std::ostringstream contents;
    contents << input.rdbuf();

    try {
        const auto root = JsonParser(contents.str()).parse();
        const auto* object = root.asObject();
        if (object == nullptr) {
            result.error = "Phoneme ONNX config root must be an object";
            return result;
        }

        result.config.sampleRateHz = numberValue(*object, "sampleRateHz", result.config.sampleRateHz);
        result.config.windowMs = numberValue(*object, "windowMs", result.config.windowMs);
        result.config.hopMs = numberValue(*object, "hopMs", result.config.hopMs);
        result.config.inputKind = parseInputKind(stringValue(*object, "inputKind", "monoPcm"));

        if (const auto labelsIt = object->find("labels"); labelsIt != object->end()) {
            if (const auto* array = labelsIt->second.asArray()) {
                for (const auto& value : *array) {
                    if (const auto* label = value.asString()) {
                        result.config.labels.push_back(*label);
                    }
                }
            }
        }

        if (result.config.labels.empty()) {
            result.error = "Phoneme ONNX config must declare at least one label";
            return result;
        }

        result.ok = true;
        return result;
    } catch (const std::exception& exception) {
        result.error = exception.what();
        return result;
    }
}

struct PhonemeOnnxBackend::Impl
{
    PhonemeOnnxRunner runner;
    std::size_t inputElements = 0;
};

PhonemeOnnxBackend::PhonemeOnnxBackend(PhonemeOnnxBackendOptions options)
    : options_(std::move(options))
    , impl_(std::make_unique<Impl>())
{
}

PhonemeOnnxBackend::~PhonemeOnnxBackend() = default;

PhonemeOnnxBackend::PhonemeOnnxBackend(PhonemeOnnxBackend&&) noexcept = default;

PhonemeOnnxBackend& PhonemeOnnxBackend::operator=(PhonemeOnnxBackend&&) noexcept = default;

bool PhonemeOnnxBackend::load(std::string& error)
{
    error.clear();
    const auto configPath = options_.configPath.empty() ? defaultConfigPathForModel(options_.modelPath)
                                                        : options_.configPath;
    const auto configResult = loadPhonemeOnnxModelConfigJson(configPath);
    if (!configResult.ok) {
        error = configResult.error;
        return false;
    }
    config_ = configResult.config;

    if (!impl_->runner.load(options_.modelPath, error)) {
        return false;
    }

    impl_->inputElements = impl_->runner.inputElementCount();
    if (impl_->inputElements == 0) {
        error = "Loaded ONNX model does not expose a static input element count";
        impl_ = std::make_unique<Impl>();
        return false;
    }

    return true;
}

bool PhonemeOnnxBackend::loaded() const noexcept
{
    return impl_ && impl_->runner.loaded();
}

const char* PhonemeOnnxBackend::name() const noexcept
{
    return "phoneme_onnx";
}

PhonemeBackendDescriptor PhonemeOnnxBackend::descriptor() const
{
    PhonemeBackendDescriptor descriptor;
    descriptor.backendName = name();
    descriptor.sampleRateHz = config_.sampleRateHz;
    descriptor.windowMs = config_.windowMs;
    descriptor.hopMs = config_.hopMs;
    descriptor.inputKind = config_.inputKind;
    descriptor.labels = config_.labels;
    descriptor.confidenceMin = 0.0;
    descriptor.confidenceMax = 1.0;
    descriptor.timestampSemantics = "frame_end_seconds";
    return descriptor;
}

std::vector<float> PhonemeOnnxBackend::prepareInput(const PhonemeBackendAudioFrame& frame,
                                                    std::size_t elementCount,
                                                    std::string& error) const
{
    std::vector<float> input(elementCount, 0.0F);

    if (config_.inputKind == PhonemeBackendInputKind::FlatTensor) {
        const auto copyCount = std::min(elementCount, frame.monoSamples.size());
        std::copy_n(frame.monoSamples.begin(), copyCount, input.begin());
        return input;
    }

    if (frame.sampleRateHz <= 0.0) {
        error = "Mono PCM input requires a positive sample rate";
        return {};
    }

    const auto windowSamples = samplesForWindow(frame.sampleRateHz, config_.windowMs);
    const auto copyCount = std::min(elementCount, windowSamples);
    const auto available = std::min(copyCount, frame.monoSamples.size());
    std::copy_n(frame.monoSamples.begin(), available, input.begin());
    if (available < copyCount) {
        error = "Audio frame shorter than configured ONNX window";
        return {};
    }

    return input;
}

PhonemeTemporalObservation PhonemeOnnxBackend::decodeOutput(const std::vector<float>& output,
                                                            double streamTimeEndSeconds) const
{
    PhonemeTemporalObservation observation;
    observation.stream_time_seconds = streamTimeEndSeconds;

    if (output.empty() || config_.labels.empty()) {
        return observation;
    }

    const std::size_t classCount = std::min(config_.labels.size(), output.size());
    std::size_t bestIndex = 0;
    double bestScore = output[0];
    for (std::size_t index = 1; index < classCount; ++index) {
        if (static_cast<double>(output[index]) > bestScore) {
            bestScore = static_cast<double>(output[index]);
            bestIndex = index;
        }
    }

    const auto& label = config_.labels[bestIndex];
    if (label == "sil" || label.empty()) {
        return observation;
    }

    observation.arpabet = label;
    observation.confidence = normalizeConfidence(bestScore, 0.0, 1.0);
    return observation;
}

PhonemeBackendResult PhonemeOnnxBackend::process(const PhonemeBackendAudioFrame& frame)
{
    PhonemeBackendResult result;
    if (!loaded()) {
        result.ok = false;
        result.error = "PhonemeOnnxBackend::load() must succeed before process()";
        return result;
    }

    const auto started = std::chrono::steady_clock::now();
    std::string prepareError;
    const auto input = prepareInput(frame, impl_->inputElements, prepareError);
    if (!prepareError.empty()) {
        result.ok = false;
        result.error = std::move(prepareError);
        return result;
    }

    const auto run = impl_->runner.run(input);
    if (!run.ok) {
        result.ok = false;
        result.error = run.error;
        return result;
    }

    const double frameDurationSeconds =
        frame.sampleRateHz > 0.0 ? static_cast<double>(frame.monoSamples.size()) / frame.sampleRateHz : 0.0;
    const double streamTimeEndSeconds = frame.streamTimeStartSeconds + frameDurationSeconds;
    result.observations.push_back(decodeOutput(run.output, streamTimeEndSeconds));

    const auto finished = std::chrono::steady_clock::now();
    result.backendLatencyMs =
        std::chrono::duration<double, std::milli>(finished - started).count();
    return result;
}

} // namespace Voice2VocalSynth
