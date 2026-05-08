#include "Voice2VocalSynth/PcmWavReader.h"

#include <cstdint>
#include <fstream>
#include <vector>

namespace Voice2VocalSynth
{
namespace
{

[[nodiscard]] bool readU32LE(std::istream& in, std::uint32_t& out)
{
    unsigned char b[4];
    if (!in.read(reinterpret_cast<char*>(b), 4)) {
        return false;
    }
    out = static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8) |
          (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
    return true;
}

[[nodiscard]] bool readU16LE(std::istream& in, std::uint16_t& out)
{
    unsigned char b[2];
    if (!in.read(reinterpret_cast<char*>(b), 2)) {
        return false;
    }
    out = static_cast<std::uint16_t>(b[0]) | (static_cast<std::uint16_t>(b[1]) << 8);
    return true;
}

[[nodiscard]] bool expectChunkId(std::istream& in, const char* id4)
{
    char buf[4];
    if (!in.read(buf, 4)) {
        return false;
    }
    return buf[0] == id4[0] && buf[1] == id4[1] && buf[2] == id4[2] && buf[3] == id4[3];
}

} // namespace

PcmWavLoadResult PcmWavReader::loadMonoFloat(const std::filesystem::path& path)
{
    PcmWavLoadResult result;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        result.error = "failed to open WAV file";
        return result;
    }

    if (!expectChunkId(in, "RIFF")) {
        result.error = "not a RIFF file";
        return result;
    }
    std::uint32_t riffSize = 0;
    if (!readU32LE(in, riffSize)) {
        result.error = "truncated RIFF size";
        return result;
    }
    if (!expectChunkId(in, "WAVE")) {
        result.error = "not a WAVE file";
        return result;
    }

    std::uint16_t audioFormat = 0;
    std::uint16_t numChannels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bitsPerSample = 0;
    bool haveFmt = false;
    std::vector<char> dataChunk;

    while (in.good()) {
        char chunkIdRaw[4];
        if (!in.read(chunkIdRaw, 4)) {
            break;
        }
        std::uint32_t chunkSize = 0;
        if (!readU32LE(in, chunkSize)) {
            result.error = "truncated chunk header";
            return result;
        }

        const std::size_t payloadBytes = chunkSize + (chunkSize % 2); // word align

        if (chunkIdRaw[0] == 'f' && chunkIdRaw[1] == 'm' && chunkIdRaw[2] == 't' &&
            chunkIdRaw[3] == ' ') {
            if (chunkSize < 16) {
                result.error = "fmt chunk too small";
                return result;
            }
            if (!readU16LE(in, audioFormat)) {
                result.error = "truncated fmt";
                return result;
            }
            if (!readU16LE(in, numChannels)) {
                result.error = "truncated fmt";
                return result;
            }
            if (!readU32LE(in, sampleRate)) {
                result.error = "truncated fmt";
                return result;
            }
            std::uint32_t byteRate = 0;
            if (!readU32LE(in, byteRate)) {
                result.error = "truncated fmt";
                return result;
            }
            std::uint16_t blockAlign = 0;
            if (!readU16LE(in, blockAlign)) {
                result.error = "truncated fmt";
                return result;
            }
            if (!readU16LE(in, bitsPerSample)) {
                result.error = "truncated fmt";
                return result;
            }
            const std::uint32_t fmtRemaining = chunkSize - 16;
            if (fmtRemaining > 0 &&
                !in.seekg(static_cast<std::streamoff>(fmtRemaining), std::ios::cur)) {
                result.error = "failed to skip fmt extension";
                return result;
            }
            if ((chunkSize % 2) == 1 && !in.seekg(1, std::ios::cur)) {
                result.error = "failed to skip fmt chunk pad";
                return result;
            }
            haveFmt = true;
            continue;
        }

        if (chunkIdRaw[0] == 'd' && chunkIdRaw[1] == 'a' && chunkIdRaw[2] == 't' &&
            chunkIdRaw[3] == 'a') {
            dataChunk.resize(chunkSize);
            if (chunkSize > 0 && !in.read(dataChunk.data(), static_cast<std::streamsize>(chunkSize))) {
                result.error = "truncated data chunk";
                return result;
            }
            if ((chunkSize % 2) == 1) {
                in.seekg(1, std::ios::cur);
            }
            continue;
        }

        if (!in.seekg(static_cast<std::streamoff>(payloadBytes), std::ios::cur)) {
            result.error = "failed to skip chunk";
            return result;
        }
    }

    if (!haveFmt) {
        result.error = "missing fmt chunk";
        return result;
    }
    if (audioFormat != 1) {
        result.error = "only PCM (format 1) WAVs are supported";
        return result;
    }
    if (bitsPerSample != 16) {
        result.error = "only 16-bit PCM is supported";
        return result;
    }
    if (numChannels < 1 || numChannels > 2) {
        result.error = "only mono or stereo WAVs are supported";
        return result;
    }
    if (sampleRate == 0 || sampleRate > 384'000) {
        result.error = "invalid sample rate";
        return result;
    }

    const std::size_t bytesPerFrame = static_cast<std::size_t>(numChannels) * 2;
    if (dataChunk.size() % bytesPerFrame != 0) {
        result.error = "data chunk size does not align to frames";
        return result;
    }

    const std::size_t numFrames = dataChunk.size() / bytesPerFrame;
    result.mono.resize(numFrames);
    result.sampleRate = static_cast<int>(sampleRate);
    result.numChannels = static_cast<int>(numChannels);

    const auto* data = reinterpret_cast<const std::uint8_t*>(dataChunk.data());
    for (std::size_t i = 0; i < numFrames; ++i) {
        if (numChannels == 1) {
            const std::int16_t s =
                static_cast<std::int16_t>(data[i * 2] | (data[i * 2 + 1] << 8));
            result.mono[i] = static_cast<float>(s) / 32768.0f;
        } else {
            const std::int16_t l = static_cast<std::int16_t>(data[i * 4] | (data[i * 4 + 1] << 8));
            const std::int16_t r = static_cast<std::int16_t>(data[i * 4 + 2] | (data[i * 4 + 3] << 8));
            result.mono[i] =
                (static_cast<float>(l) + static_cast<float>(r)) / (2.0f * 32768.0f);
        }
    }

    result.ok = true;
    return result;
}

} // namespace Voice2VocalSynth
