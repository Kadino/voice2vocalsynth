#include <Voice2VocalSynth/PhonemeBackend.h>

#include <cassert>
#include <cmath>
#include <iostream>

namespace
{
using namespace Voice2VocalSynth;

std::vector<float> sine(double frequencyHz, double sampleRateHz, int samples)
{
    std::vector<float> out(static_cast<std::size_t>(samples));
    for (int i = 0; i < samples; ++i) {
        out[static_cast<std::size_t>(i)] =
            static_cast<float>(0.25 * std::sin(2.0 * 3.14159265358979323846 *
                                               frequencyHz * static_cast<double>(i) / sampleRateHz));
    }
    return out;
}

void placeholderEmitsAhForVoicedPitch()
{
    PlaceholderPitchPhonemeBackend backend;
    PhonemeBackendAudioFrame frame;
    frame.sampleRateHz = 48000.0;
    frame.streamTimeStartSeconds = 1.0;
    frame.monoSamples = sine(220.0, frame.sampleRateHz, 4096);

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
    placeholderEmitsAhForVoicedPitch();
    placeholderEmitsSilenceForUnvoicedAudio();

    std::cout << "PhonemeBackend tests passed\n";
    return 0;
}
