#include <Voice2VocalSynth/PhonemeEvaluation.h>

#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
using namespace Voice2VocalSynth;

bool nearlyEqual(double actual, double expected, double tolerance = 0.001)
{
    return std::abs(actual - expected) <= tolerance;
}

PhonemeFrame frame(std::string arpabet, double start, double end)
{
    PhonemeFrame f;
    f.arpabet = std::move(arpabet);
    f.estimatedOnsetSeconds = start;
    f.estimatedEndSeconds = end;
    return f;
}

void scoresMatchedFrames()
{
    const auto metrics = evaluatePhonemeFrames(
        {frame("K", 0.10, 0.15), frame("AE", 0.15, 0.40)},
        {frame("K", 0.11, 0.16), frame("AE", 0.16, 0.41)});

    assert(metrics.referenceCount == 2);
    assert(metrics.predictionCount == 2);
    assert(metrics.matchedCount == 2);
    assert(metrics.missedCount == 0);
    assert(metrics.falsePositiveCount == 0);
    assert(nearlyEqual(metrics.precision, 1.0));
    assert(nearlyEqual(metrics.recall, 1.0));
    assert(nearlyEqual(metrics.f1, 1.0));
    assert(nearlyEqual(metrics.meanAbsoluteOnsetErrorMs, 10.0));
}

void rejectsWrongLabelsAndLateOnsets()
{
    PhonemeEvaluationOptions options;
    options.maxOnsetErrorSeconds = 0.03;
    const auto metrics = evaluatePhonemeFrames(
        {frame("K", 0.10, 0.15), frame("AE", 0.15, 0.40)},
        {frame("T", 0.10, 0.15), frame("AE", 0.25, 0.45)},
        options);

    assert(metrics.matchedCount == 0);
    assert(metrics.missedCount == 2);
    assert(metrics.falsePositiveCount == 2);
    assert(metrics.precision == 0.0);
    assert(metrics.recall == 0.0);
}

void handlesPartialMatches()
{
    const auto metrics = evaluatePhonemeFrames(
        {frame("K", 0.10, 0.15), frame("AE", 0.15, 0.40), frame("T", 0.40, 0.45)},
        {frame("K", 0.10, 0.16), frame("AE", 0.15, 0.38)});

    assert(metrics.matchedCount == 2);
    assert(metrics.missedCount == 1);
    assert(metrics.falsePositiveCount == 0);
    assert(nearlyEqual(metrics.precision, 1.0));
    assert(nearlyEqual(metrics.recall, 2.0 / 3.0));
}

std::filesystem::path tempJsonPath()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("voice2vocalsynth-phoneme-labels-" + std::to_string(stamp) + ".json");
}

void parsesEditablePhonemeLabelJson()
{
    const auto result = parsePhonemeFrameLabelsJson(
        "[\n"
        "  {\"arpabet\":\"K\", \"start\":0.10, \"end\":0.15, \"confidence\":0.8, \"isConsonant\":true},\n"
        "  {\"arpabet\":\"AE\", \"estimatedOnsetSeconds\":0.15, \"estimatedEndSeconds\":0.40, \"isVowel\":true}\n"
        "]\n");

    assert(result.ok);
    assert(result.frames.size() == 2);
    assert(result.frames[0].arpabet == "K");
    assert(nearlyEqual(result.frames[0].estimatedOnsetSeconds, 0.10));
    assert(nearlyEqual(result.frames[0].estimatedEndSeconds, 0.15));
    assert(result.frames[0].isConsonant);
    assert(result.frames[1].arpabet == "AE");
    assert(result.frames[1].isVowel);
}

void loadsPhonemeLabelJsonFromFile()
{
    const auto path = tempJsonPath();
    {
        std::ofstream output(path, std::ios::binary);
        output << "[{\"arpabet\":\"T\", \"start\":0.4, \"end\":0.45}]\n";
    }

    const auto result = loadPhonemeFrameLabelsJson(path);
    std::filesystem::remove(path);

    assert(result.ok);
    assert(result.frames.size() == 1);
    assert(result.frames[0].arpabet == "T");
}

void exportsMetricsJsonReport()
{
    const auto metrics = evaluatePhonemeFrames(
        {frame("K", 0.10, 0.15)},
        {frame("K", 0.11, 0.16)});
    const auto json = phonemeEvaluationMetricsToJson(metrics);

    assert(json.find("\"schemaVersion\": 1") != std::string::npos);
    assert(json.find("\"matchedCount\": 1") != std::string::npos);
    assert(json.find("\"precision\": 1.000000") != std::string::npos);
    assert(json.find("\"meanAbsoluteOnsetErrorMs\": 10.000000") != std::string::npos);
}

void rejectsMalformedLabelJson()
{
    const auto result = parsePhonemeFrameLabelsJson("{\"arpabet\":\"K\"}");
    assert(!result.ok);
    assert(!result.error.empty());
}

} // namespace

int main()
{
    scoresMatchedFrames();
    rejectsWrongLabelsAndLateOnsets();
    handlesPartialMatches();
    parsesEditablePhonemeLabelJson();
    loadsPhonemeLabelJsonFromFile();
    exportsMetricsJsonReport();
    rejectsMalformedLabelJson();

    std::cout << "PhonemeEvaluation tests passed\n";
    return 0;
}
