#pragma once

#include "Voice2VocalSynth/LatencyBudget.h"
#include "Voice2VocalSynth/PitchTarget.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Voice2VocalSynth
{

enum class OutputRoute
{
    MonitorOutput,
    ProjectVirtualMicrophone
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

struct PhonemeOnnxSettings
{
    bool enabled = false;
    bool useRepositoryTestFixture = false;
    std::string modelPath;
};

struct AppPreset
{
    int schemaVersion = 1;
    std::string name = "Default";
    AudioRoutingSettings audio;
    VoicebankSettings voicebank;
    PitchTargetOptions pitch;
    AnalysisLatencySettings latency = LatencyBudgetCalculator::presetSettings(LatencyPreset::Balanced);
    RecordingSettings recording;
    PhonemeOnnxSettings phonemeOnnx;
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
    [[nodiscard]] static std::optional<std::filesystem::path> resolvedPhonemeOnnxModelPath(const AppPreset& preset);
};

class AppPresetJson
{
public:
    [[nodiscard]] static std::string toJson(const AppPreset& preset);
    [[nodiscard]] static AppPreset fromJson(std::string_view json);
};

} // namespace Voice2VocalSynth
