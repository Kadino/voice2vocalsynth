#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Voice2VocalSynth
{

struct PcmWavWriteResult
{
    bool ok = false;
    std::string error;
};

class PcmWavWriter
{
public:
    [[nodiscard]] static PcmWavWriteResult writeMonoPcm16(const std::filesystem::path& path,
                                                          const std::vector<float>& mono,
                                                          int sampleRate);
};

} // namespace Voice2VocalSynth
