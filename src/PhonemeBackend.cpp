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

PhonemeBackendDescriptor PlaceholderPitchPhonemeBackend::descriptor() const
{
    PhonemeBackendDescriptor descriptor;
    descriptor.backendName = name();
    descriptor.sampleRateHz = options_.sampleRateHz;
    descriptor.windowMs = 85.0;
    descriptor.hopMs = 85.0;
    descriptor.inputKind = PhonemeBackendInputKind::MonoPcm;
    descriptor.labels = {"sil", options_.voicedArpabet};
    descriptor.confidenceMin = 0.0;
    descriptor.confidenceMax = 1.0;
    descriptor.timestampSemantics = "frame_end_seconds";
    return descriptor;
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
