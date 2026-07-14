#include <Voice2VocalSynth/PhonemeBakeoff.h>
#include <Voice2VocalSynth/PhonemeEvaluation.h>

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
using namespace Voice2VocalSynth;

class CommittedFrameBackend final : public IPhonemeBackend
{
public:
    [[nodiscard]] const char* name() const noexcept override
    {
        return "committed_frame_test_backend";
    }

    [[nodiscard]] PhonemeBackendDescriptor descriptor() const override
    {
        PhonemeBackendDescriptor descriptor;
        descriptor.backendName = name();
        descriptor.sampleRateHz = 16000.0;
        descriptor.windowMs = 20.0;
        descriptor.hopMs = 20.0;
        descriptor.inputKind = PhonemeBackendInputKind::MonoPcm;
        descriptor.labels = {"sil", "AE"};
        return descriptor;
    }

    [[nodiscard]] PhonemeBackendResult process(const PhonemeBackendAudioFrame& frame) override
    {
        PhonemeBackendResult result;
        if (!emitted_ && frame.streamTimeStartSeconds >= 0.20) {
            emitted_ = true;
            PhonemeFrame committed;
            committed.arpabet = "AE";
            committed.confidence = 0.9F;
            committed.estimatedOnsetSeconds = 0.20;
            committed.estimatedEndSeconds = 0.40;
            committed.isVowel = true;
            committed.isConsonant = false;
            committed.isVoiced = true;
            result.committedFrames.push_back(std::move(committed));
        }
        return result;
    }

private:
    bool emitted_ = false;
};

bool nearlyEqual(double actual, double expected, double tolerance = 0.001)
{
    return std::abs(actual - expected) <= tolerance;
}

PhonemeFrame frame(std::string arpabet, double start, double end, bool consonant = false)
{
    PhonemeFrame f;
    f.arpabet = std::move(arpabet);
    f.estimatedOnsetSeconds = start;
    f.estimatedEndSeconds = end;
    f.isConsonant = consonant;
    return f;
}

void computesP95OnsetError()
{
    PhonemeEvaluationOptions options;
    options.maxOnsetErrorSeconds = 0.20;
    const auto metrics = evaluatePhonemeFrames(
        {frame("K", 0.10, 0.15, true), frame("AE", 0.15, 0.40)},
        {frame("K", 0.11, 0.16, true), frame("AE", 0.30, 0.41)},
        options);

    assert(metrics.matchedCount == 2);
    assert(nearlyEqual(metrics.meanAbsoluteOnsetErrorMs, 80.0));
    assert(nearlyEqual(metrics.p95OnsetErrorMs, 143.0));
    assert(metrics.referenceConsonantCount == 1);
    assert(metrics.missedConsonantCount == 0);
}

void runsPlaceholderBakeoffOnSine()
{
    std::vector<float> mono(48000);
    for (std::size_t i = 0; i < mono.size(); ++i) {
        mono[i] = static_cast<float>(0.25 * std::sin(2.0 * 3.14159265358979323846 *
                                                     220.0 * static_cast<double>(i) / 48000.0));
    }

    PlaceholderPitchPhonemeBackend backend;
    std::vector<IPhonemeBackend*> backends {&backend};
    const auto report = runPhonemeBakeoff({}, backends, mono, 48000.0);
    assert(report.entries.size() == 1);
    assert(report.entries[0].backendName == backend.name());
    assert(report.entries[0].meanBackendLatencyMs >= 0.0);
}

void collectsCommittedFramesForNativeSegmentBackends()
{
    std::vector<float> mono(16000, 0.0F);
    CommittedFrameBackend backend;
    const auto pipeline = runPhonemeBackendOnMono(backend, mono, 16000.0);
    assert(pipeline.ok);
    assert(pipeline.rawFrames.size() == 1);
    assert(pipeline.rawFrames[0].arpabet == "AE");
    assert(pipeline.stabilizedFrames.size() == 1);
    assert(pipeline.stabilizedFrames[0].arpabet == "AE");
}

void flushesStabilizerCommittedFramesAtStreamEnd()
{
    std::vector<float> mono(48000);
    for (std::size_t i = 0; i < mono.size(); ++i) {
        mono[i] = static_cast<float>(0.25 * std::sin(2.0 * 3.14159265358979323846 *
                                                     220.0 * static_cast<double>(i) / 48000.0));
    }

    PlaceholderPitchPhonemeBackend backend;
    const auto pipeline = runPhonemeBackendOnMono(backend, mono, 48000.0);
    assert(pipeline.ok);
    assert(!pipeline.rawFrames.empty());
    assert(!pipeline.stabilizedFrames.empty());
    assert(pipeline.stabilizedFrames.front().arpabet == "AH");
}

} // namespace

int main()
{
    computesP95OnsetError();
    runsPlaceholderBakeoffOnSine();
    collectsCommittedFramesForNativeSegmentBackends();
    flushesStabilizerCommittedFramesAtStreamEnd();
    std::cout << "PhonemeBakeoff tests passed\n";
    return 0;
}
