#include <Voice2VocalSynth/PhonemeEvalCli.h>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
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

std::filesystem::path tempMetricsPath()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("voice2vocalsynth-phoneme-eval-" + std::to_string(stamp) + ".json");
}

void parsesRequiredArguments()
{
    std::string error;
    const auto options = parsePhonemeEvalCliArgs(
        {"Voice2VocalSynthPhonemeEval",
         "--reference",
         "ref.json",
         "--prediction",
         "pred.json",
         "--max-onset-error-ms",
         "40",
         "--min-overlap-ms",
         "15"},
        error);

    assert(options);
    assert(error.empty());
    assert(options->referencePath == "ref.json");
    assert(options->predictionPath == "pred.json");
    assert(!options->outputPath);
    assert(options->evalOptions.maxOnsetErrorSeconds == 0.04);
    assert(options->evalOptions.minOverlapSeconds == 0.015);
}

void rejectsMissingRequiredArguments()
{
    std::string error;
    const auto options = parsePhonemeEvalCliArgs({"Voice2VocalSynthPhonemeEval"}, error);
    assert(!options);
    assert(!error.empty());
}

void evaluatesFixturePair()
{
    const auto root = repositoryRoot();
    PhonemeEvalCliOptions options;
    options.referencePath = root / "tests/fixtures/phoneme_eval/reference_frames.json";
    options.predictionPath = root / "tests/fixtures/phoneme_eval/predicted_frames.json";
    options.outputPath = tempMetricsPath();

    const auto result = runPhonemeEvalCli(options);
    assert(result.exitCode == PhonemeEvalCliExitCode::Success);
    assert(result.summary.find("matched=3") != std::string::npos);
    assert(result.metricsJson);
    assert(result.metricsJson->find("\"matchedCount\": 3") != std::string::npos);

    std::ifstream written(*options.outputPath, std::ios::binary);
    assert(written.good());
    std::string contents((std::istreambuf_iterator<char>(written)), std::istreambuf_iterator<char>());
    assert(contents.find("\"f1\": 1.000000") != std::string::npos);
    std::filesystem::remove(*options.outputPath);
}

} // namespace

int main()
{
    parsesRequiredArguments();
    rejectsMissingRequiredArguments();
    evaluatesFixturePair();

    std::cout << "PhonemeEvalCli tests passed\n";
    return 0;
}
