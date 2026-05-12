#pragma once

#include "Voice2VocalSynth/RenderPlanner.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Voice2VocalSynth
{

struct OfflineRenderOptions
{
    std::filesystem::path voicebankRoot;
    int outputSampleRate = 48000;
    /// When a render event does not set `RenderEvent::sourceRecordingFrequencyHz`, this is the
    /// assumed fundamental frequency at which the bank samples were recorded (UTAU banks vary).
    double defaultSourceRecordingFrequencyHz = 261.6255653005986; // 12-TET MIDI 60 (C4).
};

struct OfflineRenderResult
{
    bool ok = false;
    std::string error;
    int sampleRate = 0;
    std::vector<float> mono {};
    std::vector<std::string> warnings {};
};

class OfflineRenderer
{
public:
    [[nodiscard]] static OfflineRenderResult render(const RenderPlan& plan,
                                                    const OfflineRenderOptions& options);
};

} // namespace Voice2VocalSynth
