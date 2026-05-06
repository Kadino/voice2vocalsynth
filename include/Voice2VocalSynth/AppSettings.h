#pragma once

#include "Voice2VocalSynth/LatencyBudget.h"
#include "Voice2VocalSynth/PitchTarget.h"

#include <string>
#include <vector>

namespace Voice2VocalSynth
{

enum class OutputRoute
{
    MonitorOutput,
    ProjectVirtualMicrophone
};

enum class AliasStylePreference
{
    AutoDetect,
    PreferRomaji,
    PreferNonAscii
};

struct AudioRoutingSettings
{
    std::string inputDeviceName;
    std::string outputDeviceName;
    OutputRoute outputRoute = OutputRoute::MonitorOutput;
};

struct VoicebankSettings
{
    std::string voicebankPath;
    std::string mappingPath;
    AliasStylePreference aliasStylePreference = AliasStylePreference::AutoDetect;
    bool allowMissingAliasFallback = true;
    std::string whistleAlias = "u";
};

struct RecordingSettings
{
    std::string privateDataFolder;
    bool optInAutoCapture = false;
    bool recordDryInput = true;
    bool recordSynthOutput = true;
    bool recordTimelineJson = true;
    bool recordPitchCsv = true;
    bool recordPhonemeCsv = true;
    bool recordAliasCsv = true;
};

struct AppPreset
{
    std::string name = "Default";
    AudioRoutingSettings audio;
    VoicebankSettings voicebank;
    PitchTargetOptions pitch;
    AnalysisLatencySettings latency = LatencyBudgetCalculator::presetSettings(LatencyPreset::Balanced);
    RecordingSettings recording;
};

struct AppSettingsValidation
{
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    [[nodiscard]] bool valid() const noexcept;
};

class AppSettingsValidator
{
public:
    [[nodiscard]] static AppSettingsValidation validate(const AppPreset& preset,
                                                        const std::string& repositoryRoot);
    [[nodiscard]] static bool pathIsInsideDirectory(const std::string& path,
                                                    const std::string& directory);
    [[nodiscard]] static AppPreset makeDefaultPreset();
};

} // namespace Voice2VocalSynth
