#include <Voice2VocalSynth/PhonemeBackend.h>

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

namespace
{
using namespace Voice2VocalSynth;

std::filesystem::path repositoryRoot()
{
#ifdef VOICE2VOCALSYNTH_REPOSITORY_ROOT
    return std::filesystem::path{VOICE2VOCALSYNTH_REPOSITORY_ROOT};
#else
    return std::filesystem::current_path();
#endif
}

void loadsModelConfigJson()
{
    const auto path = repositoryRoot() / "tests/fixtures/onnx/dummy_identity.phoneme.json";
    const auto result = loadPhonemeOnnxModelConfigJson(path);
    assert(result.ok);
    assert(result.config.inputKind == PhonemeBackendInputKind::FlatTensor);
    assert(result.config.labels.size() == 32);
    assert(result.config.labels[2] == "AE");
}

#ifdef VOICE2VOCALSYNTH_WITH_ONNX
void onnxBackendDecodesIdentityFixture()
{
    const auto root = repositoryRoot();
    PhonemeOnnxBackend backend({root / "tests/fixtures/onnx/dummy_identity.onnx",
                                root / "tests/fixtures/onnx/dummy_identity.phoneme.json"});
    std::string error;
    assert(backend.load(error));
    assert(error.empty());

    const auto descriptor = backend.descriptor();
    assert(descriptor.inputKind == PhonemeBackendInputKind::FlatTensor);
    assert(descriptor.labels.size() == 32);

    PhonemeBackendAudioFrame frame;
    frame.sampleRateHz = 48000.0;
    frame.streamTimeStartSeconds = 0.5;
    frame.monoSamples.assign(32, 0.0F);
    frame.monoSamples[2] = 1.0F;

    const auto result = backend.process(frame);
    assert(result.ok);
    assert(result.observations.size() == 1);
    assert(result.observations[0].arpabet == "AE");
    assert(result.observations[0].confidence > 0.0F);
    assert(result.observations[0].stream_time_seconds > 0.5);
}
#endif

} // namespace

int main()
{
    loadsModelConfigJson();

#ifdef VOICE2VOCALSYNTH_WITH_ONNX
    onnxBackendDecodesIdentityFixture();
    std::cout << "PhonemeOnnxBackend tests passed (ONNX enabled)\n";
#else
    std::cout << "PhonemeOnnxBackend tests passed (ONNX disabled)\n";
#endif
    return 0;
}
