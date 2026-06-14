#include <Voice2VocalSynth/PhonemeEvaluation.h>

#include <cassert>
#include <cmath>
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

} // namespace

int main()
{
    scoresMatchedFrames();
    rejectsWrongLabelsAndLateOnsets();
    handlesPartialMatches();

    std::cout << "PhonemeEvaluation tests passed\n";
    return 0;
}
