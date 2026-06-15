#include <Voice2VocalSynth/PhonemeBackend.h>

#include <cassert>
#include <cmath>
#include <iostream>

namespace
{
using namespace Voice2VocalSynth;

void placeholderDescriptorDocumentsContract()
{
    PlaceholderPitchPhonemeBackend backend;
    const auto descriptor = backend.descriptor();

    assert(descriptor.backendName == backend.name());
    assert(descriptor.sampleRateHz > 0.0);
    assert(descriptor.windowMs > 0.0);
    assert(descriptor.inputKind == PhonemeBackendInputKind::MonoPcm);
    assert(!descriptor.labels.empty());
    assert(descriptor.timestampSemantics == "frame_end_seconds");
}

void placeholderEmitsAhForVoicedPitch()
{
    PlaceholderPitchPhonemeBackend backend;
    PhonemeBackendAudioFrame frame;
    frame.sampleRateHz = 48000.0;
    frame.streamTimeStartSeconds = 1.0;
    frame.monoSamples.resize(4096);
    for (std::size_t i = 0; i < frame.monoSamples.size(); ++i) {
        frame.monoSamples[i] =
            static_cast<float>(0.25 * std::sin(2.0 * 3.14159265358979323846 *
                                                 220.0 * static_cast<double>(i) / frame.sampleRateHz));
    }

    const auto result = backend.process(frame);

    assert(result.ok);
    assert(result.observations.size() == 1);
    assert(result.observations[0].arpabet == "AH");
    assert(result.observations[0].confidence > 0.0F);
    assert(result.observations[0].stream_time_seconds > 1.0);
}

void placeholderEmitsSilenceForUnvoicedAudio()
{
    PlaceholderPitchPhonemeBackend backend;
    PhonemeBackendAudioFrame frame;
    frame.sampleRateHz = 48000.0;
    frame.monoSamples.assign(4096, 0.0F);

    const auto result = backend.process(frame);

    assert(result.ok);
    assert(result.observations.size() == 1);
    assert(result.observations[0].arpabet.empty());
    assert(result.observations[0].confidence == 0.0F);
}

} // namespace

int main()
{
    placeholderDescriptorDocumentsContract();
    placeholderEmitsAhForVoicedPitch();
    placeholderEmitsSilenceForUnvoicedAudio();

    std::cout << "PhonemeBackend tests passed\n";
    return 0;
}
