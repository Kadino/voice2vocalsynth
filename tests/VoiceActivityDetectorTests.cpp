#include <Voice2VocalSynth/VoiceActivityDetector.h>

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
using namespace Voice2VocalSynth;

void onset_and_end_with_hangover()
{
    VoiceActivityDetectorOptions opt;
    opt.onset_rms_threshold = 0.05F;
    opt.release_rms_threshold = 0.03F;
    opt.min_onset_seconds = 0.02;
    opt.hangover_seconds = 0.05;
    VoiceActivityDetector vad(opt);

    for (int i = 0; i < 5; ++i) {
        vad.observe_rms(0.1F, 0.01 * static_cast<double>(i));
    }
    SpeechBoundaryEvent ev;
    assert(vad.try_pop_boundary(ev));
    assert(ev.kind == SpeechBoundaryKind::Onset);

    for (int k = 0; k < 8; ++k) {
        vad.observe_rms(0.01F, 0.06 + 0.01 * static_cast<double>(k));
    }
    assert(vad.try_pop_boundary(ev));
    assert(ev.kind == SpeechBoundaryKind::End);
    assert(!vad.try_pop_boundary(ev));
}

void rms_from_sine_is_positive()
{
    std::vector<float> buf(256);
    for (std::size_t i = 0; i < buf.size(); ++i) {
        buf[i] = 0.5F * std::sin(static_cast<float>(i) * 0.1F);
    }
    const float rms = VoiceActivityDetector::rms_from_mono(buf.data(), static_cast<int>(buf.size()));
    assert(rms > 0.2F);
}

} // namespace

int main()
{
    onset_and_end_with_hangover();
    rms_from_sine_is_positive();
    std::cout << "VoiceActivityDetector tests passed\n";
    return 0;
}
