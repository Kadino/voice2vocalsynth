#pragma once

#include "Voice2VocalSynth/LivePhonemeVerifyPaths.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Voice2VocalSynth
{

struct LiveLogFixturePhonemeSegment
{
    std::string arpabet;
    double onsetSeconds = 0.0;
    double endSeconds = 0.0;
    bool vowel = false;
    bool consonant = false;
    float confidence = 0.8F;
};

struct LiveLogFixtureOptions
{
    LivePhonemeVerifyRunPaths runPaths;
    std::string backendCli = "pocketsphinx";
    std::string captureDevice = "LivePhonemeVerify.monitor";
    bool startupOk = true;
    std::string startupError;
    std::int64_t sessionSteadyNs = 1'000'000'000;
    double sessionStreamTimeSeconds = 0.0;
    std::int64_t playbackAnchorSteadyNs = 2'000'000'000;
    std::vector<LiveLogFixturePhonemeSegment> phonemeSegments;
    bool includeOnnxLatency = false;
    bool includeLatencyMeasure = false;
};

struct LiveLogFixtureContent
{
    std::string jsonl;
};

/// Maps shell `--phoneme-backend` values to the `session_start` / `ph_frame` backend field.
[[nodiscard]] std::string liveLogFixtureSessionBackend(std::string_view backendCli);

/// MFA sample TextGrid timing (K / AE / T) for harness alignment.
[[nodiscard]] std::vector<LiveLogFixturePhonemeSegment> defaultMfaSamplePhonemeSegments();

[[nodiscard]] LiveLogFixtureContent generateLiveLogFixture(const LiveLogFixtureOptions& options);

[[nodiscard]] bool writeLiveLogFixture(const LiveLogFixtureOptions& options, std::string& error);

} // namespace Voice2VocalSynth
