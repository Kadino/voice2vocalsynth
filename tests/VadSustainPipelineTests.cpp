#include <Voice2VocalSynth/InferenceLatencyTracker.h>
#include <Voice2VocalSynth/LatencyBudget.h>
#include <Voice2VocalSynth/PitchTarget.h>
#include <Voice2VocalSynth/PlaybackBoundaryMapper.h>
#include <Voice2VocalSynth/StreamingLiveRenderer.h>
#include <Voice2VocalSynth/UtteranceSustainReleasePolicy.h>
#include <Voice2VocalSynth/VoiceActivityDetector.h>

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace
{
using namespace Voice2VocalSynth;

void writePcmWavMono(const std::filesystem::path& path, int sampleRate, const std::vector<std::int16_t>& frames)
{
    std::ofstream out(path, std::ios::binary);
    assert(out);

    const std::uint32_t dataBytes = static_cast<std::uint32_t>(frames.size() * sizeof(std::int16_t));
    const std::uint32_t riffChunkSize = 4 + (8 + 16) + (8 + dataBytes);

    out.write("RIFF", 4);
    const auto writeU32 = [&out](std::uint32_t v) {
        char b[4] = {static_cast<char>(v & 0xff),
                       static_cast<char>((v >> 8) & 0xff),
                       static_cast<char>((v >> 16) & 0xff),
                       static_cast<char>((v >> 24) & 0xff)};
        out.write(b, 4);
    };
    const auto writeU16 = [&out](std::uint16_t v) {
        char b[2] = {static_cast<char>(v & 0xff), static_cast<char>((v >> 8) & 0xff)};
        out.write(b, 2);
    };

    writeU32(riffChunkSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    writeU32(16);
    writeU16(1);
    writeU16(1);
    writeU32(static_cast<std::uint32_t>(sampleRate));
    writeU32(static_cast<std::uint32_t>(sampleRate * 2));
    writeU16(2);
    writeU16(16);
    out.write("data", 4);
    writeU32(dataBytes);
    for (std::int16_t sample : frames) {
        writeU16(static_cast<std::uint16_t>(sample));
    }
}

std::filesystem::path makeTempVoicebankRoot()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto root = std::filesystem::temp_directory_path() /
                ("voice2vocalsynth-vad-pipeline-" + std::to_string(stamp));
    std::filesystem::create_directories(root);

    std::ofstream(root / "oto.ini", std::ios::binary)
        << "a.wav=a,0,50,0,30,5\n";

    std::vector<std::int16_t> samples(4800);
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const double phase = 2.0 * 3.14159265358979323846 * static_cast<double>(index) / 80.0;
        samples[index] = static_cast<std::int16_t>(std::lround(std::sin(phase) * 12000.0));
    }
    writePcmWavMono(root / "a.wav", 48000, samples);
    return root;
}

void vadThroughReleaseTruncatesLiveRenderer()
{
    VoiceActivityDetectorOptions vadOptions;
    vadOptions.onset_rms_threshold = 0.05F;
    vadOptions.release_rms_threshold = 0.03F;
    vadOptions.min_onset_seconds = 0.02;
    vadOptions.hangover_seconds = 0.05;
    VoiceActivityDetector vad(vadOptions);

    const auto settings = LatencyBudgetCalculator::presetSettings(LatencyPreset::Balanced);
    const auto breakdown = LatencyBudgetCalculator::calculate({}, settings);

    InferenceLatencyTracker latencyTracker;
    latencyTracker.observe_ms(18.0);
    latencyTracker.observe_ms(22.0);
    const double jitterMs = latencyTracker.estimate_ms();

    UtteranceSustainReleasePolicy releasePolicy;

    const auto voicebankRoot = makeTempVoicebankRoot();
    StreamingLiveRendererOptions rendererOptions;
    rendererOptions.outputSampleRate = 48000;
    rendererOptions.defaultEventDurationMs = 500.0;
    StreamingLiveRenderer renderer(rendererOptions);
    std::string error;
    assert(renderer.configure(voicebankRoot, std::nullopt, error));

    PhonemeFrame frame;
    frame.arpabet = "AE";
    frame.estimatedOnsetSeconds = 0.0;
    frame.estimatedEndSeconds = 0.5;
    frame.isVowel = true;

    PitchTarget pitchTarget;
    pitchTarget.displayNoteName = "C4";
    pitchTarget.targetFrequencyHz = PitchTargetCalculator::midiToFrequency(60.0);
    pitchTarget.targetMidi = 60.0;
    pitchTarget.displayMidiNote = 60;

    renderer.onUtteranceStart();
    renderer.onCommittedPhoneme(frame, pitchTarget, 0.0, breakdown.endToEndMonitoringLatencyMs());

    double playbackNow = 0.0;
    const double stepSeconds = 0.01;
    std::optional<double> releasePlaybackTime;
    std::optional<double> releaseAnalysisEnd;

    for (int step = 0; step < 40; ++step) {
        const double streamTime = step * stepSeconds;
        const float rms = (streamTime >= 0.02 && streamTime < 0.12) ? 0.10F : 0.01F;
        vad.observe_rms(rms, streamTime);

        SpeechBoundaryEvent boundary;
        while (vad.try_pop_boundary(boundary)) {
            releasePolicy.on_speech_boundary(boundary, breakdown, jitterMs);
        }

        SustainReleaseCommand releaseCmd;
        if (releasePolicy.try_pop_release(playbackNow, releaseCmd)) {
            releasePlaybackTime = releaseCmd.playback_time_seconds;
            releaseAnalysisEnd = releaseCmd.analysis_end_seconds;
            renderer.onSustainRelease(releaseCmd.playback_time_seconds);
        }

        playbackNow += stepSeconds;
    }

    assert(releasePlaybackTime.has_value());
    assert(releaseAnalysisEnd.has_value());

    const double expectedRelease = PlaybackBoundaryMapper::analysisToPlaybackSeconds(
        *releaseAnalysisEnd, breakdown, jitterMs);
    assert(std::abs(*releasePlaybackTime - expectedRelease) < 1.0e-6);

    std::vector<float> afterRelease(3840, 0.0F);
    renderer.renderBlock(afterRelease.data(),
                         static_cast<int>(afterRelease.size()),
                         *releasePlaybackTime + 0.05,
                         48000.0);
    float peak = 0.0F;
    for (float sample : afterRelease) {
        peak = std::max(peak, std::fabs(sample));
    }
    assert(peak < 0.001F);

    std::filesystem::remove_all(voicebankRoot);
}

} // namespace

int main()
{
    vadThroughReleaseTruncatesLiveRenderer();
    std::cout << "VadSustainPipeline tests passed\n";
    return 0;
}
