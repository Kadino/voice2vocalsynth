#include <Voice2VocalSynth/PhonemeOnnxRunner.h>

#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

namespace
{
using namespace Voice2VocalSynth;

void stubRunnerRejectsLoadWhenOnnxDisabled()
{
    PhonemeOnnxRunner runner;
    std::string error;
    assert(!runner.load(std::filesystem::path{"does-not-exist.onnx"}, error));
    assert(!error.empty());
}

#ifdef VOICE2VOCALSYNTH_WITH_ONNX
void identityFixtureRoundTrip()
{
    PhonemeOnnxRunner runner;
    std::string error;
    const std::filesystem::path model{VOICE2VOCALSYNTH_TEST_ONNX_MODEL};
    assert(runner.load(model, error));
    assert(error.empty());

    std::vector<float> input(32, 0.25F);
    const auto result = runner.run(input);
    assert(result.ok);
    assert(result.error.empty());
    assert(result.output_shape.size() == 3);
    assert(result.output_shape[0] == 1);
    assert(result.output_shape[1] == 4);
    assert(result.output_shape[2] == 8);
    assert(result.output.size() == 32);
    for (std::size_t i = 0; i < result.output.size(); ++i) {
        assert(std::fabs(static_cast<double>(result.output[i] - input[i])) < 1.0e-5);
    }
}
#endif

} // namespace

int main()
{
    stubRunnerRejectsLoadWhenOnnxDisabled();

#ifdef VOICE2VOCALSYNTH_WITH_ONNX
    identityFixtureRoundTrip();
    std::cout << "PhonemeOnnxRunner tests passed (ONNX enabled)\n";
#else
    std::cout << "PhonemeOnnxRunner tests passed (ONNX disabled)\n";
#endif
    return 0;
}
