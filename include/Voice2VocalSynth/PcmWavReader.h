#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Voice2VocalSynth
{

struct PcmWavLoadResult
{
    bool ok = false;
    std::string error;
    int sampleRate = 0;
    int numChannels = 0;
    std::vector<float> mono {}; // mono samples, nominal range ~[-1, 1]
};

class PcmWavReader
{
public:
    [[nodiscard]] static PcmWavLoadResult loadMonoFloat(const std::filesystem::path& path);
};

} // namespace Voice2VocalSynth
