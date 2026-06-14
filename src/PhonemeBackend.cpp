#include "Voice2VocalSynth/PhonemeBackend.h"

#include <utility>

namespace Voice2VocalSynth
{

PlaceholderPitchPhonemeBackend::PlaceholderPitchPhonemeBackend(
    PlaceholderPitchPhonemeBackendOptions options)
    : options_(std::move(options))
{
}

const char* PlaceholderPitchPhonemeBackend::name() const noexcept
{
    return "placeholder_pitch_gate";
}

PhonemeBackendResult PlaceholderPitchPhonemeBackend::process(const PhonemeBackendAudioFrame& frame)
{
    PhonemeBackendResult result;
    const auto estimate = estimatePitchFromMono(frame.monoSamples.data(),
                                                static_cast<int>(frame.monoSamples.size()),
                                                frame.sampleRateHz);

    PhonemeTemporalObservation observation;
    observation.stream_time_seconds =
        frame.streamTimeStartSeconds +
        (frame.sampleRateHz > 0.0 ? static_cast<double>(frame.monoSamples.size()) / frame.sampleRateHz : 0.0);

    if (estimate.confidence >= options_.minPitchConfidence) {
        observation.arpabet = options_.voicedArpabet;
        observation.confidence = static_cast<float>(estimate.confidence);
    }

    result.observations.push_back(std::move(observation));
    return result;
}

} // namespace Voice2VocalSynth
