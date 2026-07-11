#include <Voice2VocalSynth/PocketSphinxPhonemeBackend.h>

#include <algorithm>
#include <cassert>
#include <iostream>

namespace
{
using namespace Voice2VocalSynth;

void exposesArpabetStreamingContract()
{
    PocketSphinxPhonemeBackend backend;
    const auto descriptor = backend.descriptor();
    assert(descriptor.backendName == "pocketsphinx_allphone");
    assert(descriptor.sampleRateHz == 16000.0);
    assert(descriptor.hopMs == 10.0);
    assert(std::find(descriptor.labels.begin(), descriptor.labels.end(), "AA") !=
           descriptor.labels.end());
    assert(std::find(descriptor.labels.begin(), descriptor.labels.end(), "ZH") !=
           descriptor.labels.end());
}

void loadsPinnedModelAndProcessesOverlappingFrames()
{
    PocketSphinxPhonemeBackend backend;
    std::string error;
#if defined(VOICE2VOCALSYNTH_WITH_POCKETSPHINX)
    assert(backend.load(error));
    assert(error.empty());
    PhonemeBackendAudioFrame first;
    first.sampleRateHz = 48000.0;
    first.streamTimeStartSeconds = 0.0;
    first.monoSamples.assign(4096, 0.0F);
    const auto firstResult = backend.process(first);
    assert(firstResult.ok);
    assert(firstResult.observations.size() == 1);

    auto second = first;
    second.streamTimeStartSeconds = 0.05;
    const auto secondResult = backend.process(second);
    assert(secondResult.ok);
    assert(secondResult.observations.size() == 1);
    assert(secondResult.observations[0].stream_time_seconds >
           firstResult.observations[0].stream_time_seconds);
    backend.reset();
#else
    assert(!backend.load(error));
    assert(!error.empty());
#endif
}

} // namespace

int main()
{
    exposesArpabetStreamingContract();
    loadsPinnedModelAndProcessesOverlappingFrames();
    std::cout << "PocketSphinxPhonemeBackend tests passed\n";
    return 0;
}
