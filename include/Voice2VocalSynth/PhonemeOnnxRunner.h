#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Voice2VocalSynth
{

/// Loads a single ONNX model and runs one CPU inference per call.
/// Intended as the integration point for streaming phoneme models (see spec).
///
/// When the project is built without `VOICE2VOCALSYNTH_WITH_ONNX`, all loads fail
/// with a descriptive error string.
class PhonemeOnnxRunner
{
public:
    PhonemeOnnxRunner();
    ~PhonemeOnnxRunner();

    PhonemeOnnxRunner(const PhonemeOnnxRunner&) = delete;
    PhonemeOnnxRunner& operator=(const PhonemeOnnxRunner&) = delete;
    PhonemeOnnxRunner(PhonemeOnnxRunner&&) noexcept;
    PhonemeOnnxRunner& operator=(PhonemeOnnxRunner&&) noexcept;

    [[nodiscard]] bool loaded() const noexcept;

    /// Loads an ONNX model from disk. Thread-hostile: call from one thread only.
    [[nodiscard]] bool load(const std::filesystem::path& model_path, std::string& error);

    struct RunResult
    {
        bool ok = false;
        std::string error;
        std::vector<float> output;
        std::vector<std::int64_t> output_shape;
    };

    /// Runs one forward pass. `input` must match the flat element count of the
    /// first input tensor (static shape only; dynamic dimensions are not supported yet).
    [[nodiscard]] RunResult run(const std::vector<float>& input);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Voice2VocalSynth
