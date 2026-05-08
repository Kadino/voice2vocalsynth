#include <Voice2VocalSynth/OfflineRenderer.h>
#include <Voice2VocalSynth/PcmWavReader.h>
#include <Voice2VocalSynth/PcmWavWriter.h>

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
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
    for (std::int16_t s : frames) {
        writeU16(static_cast<std::uint16_t>(s));
    }
}

void pcmWavReaderLoadsMono()
{
    const auto dir = std::filesystem::temp_directory_path() / "v2vs_pcm_test";
    std::filesystem::create_directories(dir);
    const auto wav = dir / "tone.wav";
    std::vector<std::int16_t> samples(100);
    for (int i = 0; i < 100; ++i) {
        samples[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(i * 100);
    }
    writePcmWavMono(wav, 1000, samples);

    const auto loaded = PcmWavReader::loadMonoFloat(wav);
    assert(loaded.ok);
    assert(loaded.sampleRate == 1000);
    assert(loaded.mono.size() == 100);
    assert(loaded.mono[50] > loaded.mono[10]);
}

void offlineRendererPlacesEventOnTimeline()
{
    const auto dir = std::filesystem::temp_directory_path() / "v2vs_offline_test";
    std::filesystem::create_directories(dir);
    const auto wav = dir / "seg.wav";
    std::vector<std::int16_t> samples(200);
    for (int i = 0; i < 200; ++i) {
        samples[static_cast<std::size_t>(i)] = 4000;
    }
    writePcmWavMono(wav, 1000, samples);

    RenderPlan plan;
    RenderEvent ev;
    ev.alias = "test";
    ev.wavFile = "seg.wav";
    ev.startTimeSeconds = 0.05;
    ev.durationMs = 40.0;
    ev.otoTiming.offsetMs = 0.0;
    ev.otoTiming.cutoffMs = 0.0;
    plan.events.push_back(ev);

    OfflineRenderOptions opts;
    opts.voicebankRoot = dir;
    opts.outputSampleRate = 1000;

    const auto rendered = OfflineRenderer::render(plan, opts);
    assert(rendered.ok);
    assert(rendered.sampleRate == 1000);
    assert(rendered.mono.size() == 90);
    assert(rendered.warnings.empty());

    const std::size_t i0 = static_cast<std::size_t>(0.05 * 1000.0);
    assert(rendered.mono[i0] > 0.05f);
    assert(rendered.mono[0] < 0.001f);
}

void pcmWavWriterRoundTrip()
{
    const auto dir = std::filesystem::temp_directory_path() / "v2vs_wav_write_test";
    std::filesystem::create_directories(dir);
    const auto wav = dir / "out.wav";
    std::vector<float> samples(64);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        samples[i] = static_cast<float>(i) / static_cast<float>(samples.size()) * 0.5f;
    }
    const auto wr = PcmWavWriter::writeMonoPcm16(wav, samples, 48000);
    assert(wr.ok);

    const auto loaded = PcmWavReader::loadMonoFloat(wav);
    assert(loaded.ok);
    assert(loaded.sampleRate == 48000);
    assert(loaded.mono.size() == samples.size());
    assert(loaded.mono.back() > loaded.mono.front());
}

void offlineRendererOverlapCrossfadesIntoPreviousNote()
{
    const auto dir = std::filesystem::temp_directory_path() / "v2vs_offline_overlap_test";
    std::filesystem::create_directories(dir);
    const auto wavHi = dir / "hi.wav";
    const auto wavLo = dir / "lo.wav";
    std::vector<std::int16_t> hi(200, 12000);
    std::vector<std::int16_t> lo(200, -12000);
    writePcmWavMono(wavHi, 1000, hi);
    writePcmWavMono(wavLo, 1000, lo);

    RenderPlan plan;
    RenderEvent first;
    first.alias = "hi";
    first.wavFile = "hi.wav";
    first.startTimeSeconds = 0.0;
    first.durationMs = 50.0;
    first.otoTiming.offsetMs = 0.0;
    first.otoTiming.cutoffMs = 0.0;
    plan.events.push_back(first);

    RenderEvent second;
    second.alias = "lo";
    second.wavFile = "lo.wav";
    second.startTimeSeconds = 0.05;
    second.durationMs = 50.0;
    second.otoTiming.offsetMs = 0.0;
    second.otoTiming.cutoffMs = 0.0;
    second.otoTiming.overlapMs = 20.0;
    plan.events.push_back(second);

    OfflineRenderOptions opts;
    opts.voicebankRoot = dir;
    opts.outputSampleRate = 1000;

    const auto rendered = OfflineRenderer::render(plan, opts);
    assert(rendered.ok);
    assert(rendered.sampleRate == 1000);
    assert(rendered.warnings.empty());
    assert(rendered.mono.size() == 100);

    assert(rendered.mono[10] > 0.25f);
    assert(rendered.mono[10] < 0.45f);

    assert(rendered.mono[30] > 0.15f);
    assert(rendered.mono[49] < -0.15f);

    assert(rendered.mono[75] < -0.25f);
    assert(rendered.mono[75] > -0.45f);

    for (std::size_t k = 31; k < 49; ++k) {
        assert(rendered.mono[k] < rendered.mono[k - 1]);
    }
}

void offlineRendererSkipsMissingWavWithWarning()
{
    RenderPlan plan;
    RenderEvent ev;
    ev.alias = "x";
    ev.wavFile = "nope.wav";
    ev.startTimeSeconds = 0.0;
    ev.durationMs = 10.0;
    plan.events.push_back(ev);

    OfflineRenderOptions opts;
    opts.voicebankRoot = std::filesystem::temp_directory_path();
    opts.outputSampleRate = 48000;

    const auto rendered = OfflineRenderer::render(plan, opts);
    assert(rendered.ok);
    assert(rendered.warnings.size() == 1);
}

} // namespace

int main()
{
    pcmWavReaderLoadsMono();
    offlineRendererPlacesEventOnTimeline();
    offlineRendererOverlapCrossfadesIntoPreviousNote();
    pcmWavWriterRoundTrip();
    offlineRendererSkipsMissingWavWithWarning();
    return 0;
}
