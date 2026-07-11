#pragma once

#include "Voice2VocalSynth/PhonemeTemporalStabilizer.h"
#include "Voice2VocalSynth/SimplePitchEstimator.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Voice2VocalSynth
{

/// How a backend consumes each `PhonemeBackendAudioFrame`.
enum class PhonemeBackendInputKind
{
  MonoPcm,
  FlatTensor,
};

/// Model-facing assumptions exposed by every phoneme backend.
struct PhonemeBackendDescriptor
{
  std::string backendName;
  double sampleRateHz = 0.0;
  double windowMs = 0.0;
  double hopMs = 0.0;
  PhonemeBackendInputKind inputKind = PhonemeBackendInputKind::MonoPcm;
  std::vector<std::string> labels;
  /// Confidence values are normalized to [0, 1] before reaching the stabilizer.
  double confidenceMin = 0.0;
  double confidenceMax = 1.0;
  /// `PhonemeTemporalObservation::stream_time_seconds` marks the frame end on the capture clock.
  std::string timestampSemantics = "frame_end_seconds";
};

struct PhonemeBackendAudioFrame
{
  std::vector<float> monoSamples;
  double sampleRateHz = 0.0;
  double streamTimeStartSeconds = 0.0;
};

struct PhonemeBackendResult
{
  bool ok = true;
  std::string error;
  std::vector<PhonemeTemporalObservation> observations;
  /// Backends with native stable segment boundaries may bypass the generic
  /// observation stabilizer and return committed frames directly.
  std::vector<PhonemeFrame> committedFrames;
  double backendLatencyMs = 0.0;
};

class IPhonemeBackend
{
public:
  virtual ~IPhonemeBackend() = default;

  [[nodiscard]] virtual const char* name() const noexcept = 0;
  [[nodiscard]] virtual PhonemeBackendDescriptor descriptor() const = 0;
  /// Clears stream-specific decoder state when capture restarts.
  virtual void reset() {}
  [[nodiscard]] virtual PhonemeBackendResult process(const PhonemeBackendAudioFrame& frame) = 0;
};

struct PlaceholderPitchPhonemeBackendOptions
{
  std::string voicedArpabet = "AH";
  float minPitchConfidence = 0.45F;
  double sampleRateHz = 48000.0;
};

/// Debug backend matching the current live-shell placeholder path:
/// confident voiced pitch -> one `AH`-like observation, otherwise silence.
class PlaceholderPitchPhonemeBackend final : public IPhonemeBackend
{
public:
  explicit PlaceholderPitchPhonemeBackend(PlaceholderPitchPhonemeBackendOptions options = {});

  [[nodiscard]] const char* name() const noexcept override;
  [[nodiscard]] PhonemeBackendDescriptor descriptor() const override;
  [[nodiscard]] PhonemeBackendResult process(const PhonemeBackendAudioFrame& frame) override;

private:
  PlaceholderPitchPhonemeBackendOptions options_;
};

/// Sidecar JSON contract for ONNX phoneme models (see docs/live-pipeline-roadmap.md).
struct PhonemeOnnxModelConfig
{
  double sampleRateHz = 16000.0;
  double windowMs = 160.0;
  double hopMs = 20.0;
  PhonemeBackendInputKind inputKind = PhonemeBackendInputKind::MonoPcm;
  std::vector<std::string> labels;
};

struct PhonemeOnnxModelConfigLoadResult
{
  bool ok = false;
  std::string error;
  PhonemeOnnxModelConfig config;
};

[[nodiscard]] PhonemeOnnxModelConfigLoadResult loadPhonemeOnnxModelConfigJson(
    const std::filesystem::path& path);

struct PhonemeOnnxBackendOptions
{
  std::filesystem::path modelPath;
  std::filesystem::path configPath;
};

/// Adapter that converts ONNX Runtime tensor output into phoneme observations.
class PhonemeOnnxBackend final : public IPhonemeBackend
{
public:
  explicit PhonemeOnnxBackend(PhonemeOnnxBackendOptions options);
  ~PhonemeOnnxBackend() override;

  PhonemeOnnxBackend(const PhonemeOnnxBackend&) = delete;
  PhonemeOnnxBackend& operator=(const PhonemeOnnxBackend&) = delete;
  PhonemeOnnxBackend(PhonemeOnnxBackend&&) noexcept;
  PhonemeOnnxBackend& operator=(PhonemeOnnxBackend&&) noexcept;

  [[nodiscard]] bool load(std::string& error);
  [[nodiscard]] bool loaded() const noexcept;

  [[nodiscard]] const char* name() const noexcept override;
  [[nodiscard]] PhonemeBackendDescriptor descriptor() const override;
  [[nodiscard]] PhonemeBackendResult process(const PhonemeBackendAudioFrame& frame) override;

private:
  [[nodiscard]] std::vector<float> prepareInput(const PhonemeBackendAudioFrame& frame,
                                                std::size_t elementCount,
                                                std::string& error) const;
  [[nodiscard]] PhonemeTemporalObservation decodeOutput(const std::vector<float>& output,
                                                          double streamTimeEndSeconds) const;

  PhonemeOnnxBackendOptions options_;
  PhonemeOnnxModelConfig config_;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace Voice2VocalSynth
