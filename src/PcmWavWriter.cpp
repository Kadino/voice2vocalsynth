#include "Voice2VocalSynth/PcmWavWriter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>

namespace Voice2VocalSynth
{
namespace
{

void writeU32LE(std::ostream& out, std::uint32_t v)
{
    char b[4] = {static_cast<char>(v & 0xff),
                 static_cast<char>((v >> 8) & 0xff),
                 static_cast<char>((v >> 16) & 0xff),
                 static_cast<char>((v >> 24) & 0xff)};
    out.write(b, 4);
}

void writeU16LE(std::ostream& out, std::uint16_t v)
{
    char b[2] = {static_cast<char>(v & 0xff), static_cast<char>((v >> 8) & 0xff)};
    out.write(b, 2);
}

} // namespace

PcmWavWriteResult PcmWavWriter::writeMonoPcm16(const std::filesystem::path& path,
                                                 const std::vector<float>& mono,
                                                 int sampleRate)
{
    PcmWavWriteResult result;
    if (sampleRate <= 0) {
        result.error = "sampleRate must be positive";
        return result;
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        result.error = "failed to open output WAV path";
        return result;
    }

    const std::uint32_t dataBytes =
        static_cast<std::uint32_t>(mono.size() * sizeof(std::int16_t));
    const std::uint32_t riffChunkSize = 4 + (8 + 16) + (8 + dataBytes);

    out.write("RIFF", 4);
    writeU32LE(out, riffChunkSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    writeU32LE(out, 16);
    writeU16LE(out, 1);
    writeU16LE(out, 1);
    writeU32LE(out, static_cast<std::uint32_t>(sampleRate));
    writeU32LE(out, static_cast<std::uint32_t>(sampleRate * 2));
    writeU16LE(out, 2);
    writeU16LE(out, 16);
    out.write("data", 4);
    writeU32LE(out, dataBytes);

    for (float s : mono) {
        const float c = std::clamp(s, -1.0f, 1.0f);
        auto sample = static_cast<std::int32_t>(std::lround(static_cast<double>(c) * 32767.0));
        sample = std::clamp(sample, static_cast<std::int32_t>(-32768), static_cast<std::int32_t>(32767));
        writeU16LE(out, static_cast<std::uint16_t>(static_cast<std::int16_t>(sample)));
    }

    result.ok = true;
    return result;
}

} // namespace Voice2VocalSynth
