#include <Voice2VocalSynth/PitchTarget.h>
#include <Voice2VocalSynth/StreamingLiveRenderer.h>

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
                ("voice2vocalsynth-streaming-live-" + std::to_string(stamp));
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

void schedulesVowelAndRendersAudio()
{
    const auto voicebankRoot = makeTempVoicebankRoot();
    StreamingLiveRenderer renderer;
    std::string error;
    assert(renderer.configure(voicebankRoot, std::nullopt, error));
    assert(error.empty());
    assert(renderer.configured());
    assert(renderer.warnings().empty());

    PhonemeFrame frame;
    frame.arpabet = "AE";
    frame.estimatedOnsetSeconds = 0.0;
    frame.estimatedEndSeconds = 0.12;
    frame.isVowel = true;

    PitchTarget pitchTarget;
    pitchTarget.displayNoteName = "C4";
    pitchTarget.targetFrequencyHz = PitchTargetCalculator::midiToFrequency(60.0);
    pitchTarget.targetMidi = 60.0;
    pitchTarget.displayMidiNote = 60;

    renderer.onUtteranceStart();
    renderer.onCommittedPhoneme(frame, pitchTarget, 0.0, 25.0);
    assert(renderer.lastTimeline().has_value());
    assert(renderer.warnings().empty());

    std::vector<float> output(480, 0.0F);
    renderer.renderBlock(output.data(), static_cast<int>(output.size()), 0.0, 48000.0);

    float peak = 0.0F;
    for (float sample : output) {
        peak = std::max(peak, std::fabs(sample));
    }
    assert(peak > 0.01F);

    std::filesystem::remove_all(voicebankRoot);
}

void skipsDuplicateConsecutivePhonemes()
{
    const auto voicebankRoot = makeTempVoicebankRoot();
    StreamingLiveRenderer renderer;
    std::string error;
    assert(renderer.configure(voicebankRoot, std::nullopt, error));

    PhonemeFrame frame;
    frame.arpabet = "AE";
    frame.estimatedOnsetSeconds = 0.0;
    frame.estimatedEndSeconds = 0.12;
    frame.isVowel = true;

    PitchTarget pitchTarget;
    pitchTarget.displayNoteName = "C4";
    pitchTarget.targetFrequencyHz = PitchTargetCalculator::midiToFrequency(60.0);
    pitchTarget.targetMidi = 60.0;
    pitchTarget.displayMidiNote = 60;

    renderer.onUtteranceStart();
    renderer.onCommittedPhoneme(frame, pitchTarget, 0.0, 25.0);
    renderer.onCommittedPhoneme(frame, pitchTarget, 0.12, 25.0);
    assert(renderer.lastTimeline().has_value());

    std::filesystem::remove_all(voicebankRoot);
}

void sustainReleaseTruncatesScheduledAudio()
{
    const auto voicebankRoot = makeTempVoicebankRoot();
    StreamingLiveRendererOptions options;
    options.outputSampleRate = 48000;
    options.defaultEventDurationMs = 500.0;
    StreamingLiveRenderer renderer(options);
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
    renderer.onSustainRelease(0.08);
    renderer.onCommittedPhoneme(frame, pitchTarget, 0.0, 25.0);

    std::vector<float> earlyBlock(3840, 0.0F);
    renderer.renderBlock(earlyBlock.data(), static_cast<int>(earlyBlock.size()), 0.0, 48000.0);
    float earlyPeak = 0.0F;
    for (float sample : earlyBlock) {
        earlyPeak = std::max(earlyPeak, std::fabs(sample));
    }
    assert(earlyPeak > 0.01F);

    std::vector<float> lateBlock(3840, 0.0F);
    renderer.renderBlock(lateBlock.data(), static_cast<int>(lateBlock.size()), 0.12, 48000.0);
    float latePeak = 0.0F;
    for (float sample : lateBlock) {
        latePeak = std::max(latePeak, std::fabs(sample));
    }
    assert(latePeak < 0.001F);

    std::filesystem::remove_all(voicebankRoot);
}

void utteranceStartClearsSustainRelease()
{
    const auto voicebankRoot = makeTempVoicebankRoot();
    StreamingLiveRenderer renderer;
    std::string error;
    assert(renderer.configure(voicebankRoot, std::nullopt, error));

    PhonemeFrame frame;
    frame.arpabet = "AE";
    frame.estimatedOnsetSeconds = 0.0;
    frame.estimatedEndSeconds = 0.12;
    frame.isVowel = true;

    PitchTarget pitchTarget;
    pitchTarget.displayNoteName = "C4";
    pitchTarget.targetFrequencyHz = PitchTargetCalculator::midiToFrequency(60.0);
    pitchTarget.targetMidi = 60.0;
    pitchTarget.displayMidiNote = 60;

    renderer.onUtteranceStart();
    renderer.onSustainRelease(0.05);
    renderer.onUtteranceStart();
    renderer.onCommittedPhoneme(frame, pitchTarget, 0.0, 25.0);

    std::vector<float> lateBlock(9600, 0.0F);
    renderer.renderBlock(lateBlock.data(), static_cast<int>(lateBlock.size()), 0.10, 48000.0);
    float latePeak = 0.0F;
    for (float sample : lateBlock) {
        latePeak = std::max(latePeak, std::fabs(sample));
    }
    assert(latePeak > 0.01F);

    std::filesystem::remove_all(voicebankRoot);
}

} // namespace

int main()
{
    schedulesVowelAndRendersAudio();
    skipsDuplicateConsecutivePhonemes();
    sustainReleaseTruncatesScheduledAudio();
    utteranceStartClearsSustainRelease();
    std::cout << "StreamingLiveRenderer tests passed\n";
    return 0;
}
