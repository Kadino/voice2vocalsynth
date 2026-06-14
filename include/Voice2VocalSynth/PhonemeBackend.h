#pragma once

#include "Voice2VocalSynth/PhonemeTemporalStabilizer.h"
#include "Voice2VocalSynth/SimplePitchEstimator.h"

#include <string>
#include <vector>

namespace Voice2VocalSynth
{

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
    double backendLatencyMs = 0.0;
};

class IPhonemeBackend
{
public:
    virtual ~IPhonemeBackend() = default;

    [[nodiscard]] virtual const char* name() const noexcept = 0;
    [[nodiscard]] virtual PhonemeBackendResult process(const PhonemeBackendAudioFrame& frame) = 0;
};

struct PlaceholderPitchPhonemeBackendOptions
{
    std::string voicedArpabet = "AH";
    float minPitchConfidence = 0.45F;
};

/// Debug backend matching the current live-shell placeholder path:
/// confident voiced pitch -> one `AH`-like observation, otherwise silence.
class PlaceholderPitchPhonemeBackend final : public IPhonemeBackend
{
public:
    explicit PlaceholderPitchPhonemeBackend(PlaceholderPitchPhonemeBackendOptions options = {});

    [[nodiscard]] const char* name() const noexcept override;
    [[nodiscard]] PhonemeBackendResult process(const PhonemeBackendAudioFrame& frame) override;

private:
    PlaceholderPitchPhonemeBackendOptions options_;
};

} // namespace Voice2VocalSynth
