#include <Voice2VocalSynth/AppSettings.h>
#include <Voice2VocalSynth/PitchTarget.h>

#include <cassert>
#include <iostream>
#include <string>

namespace
{
using namespace Voice2VocalSynth;

void defaultPresetUsesSafeLiveDefaults()
{
    const auto preset = AppSettingsValidator::makeDefaultPreset();

    assert(preset.schemaVersion == 1);
    assert(preset.name == "Default");
    assert(preset.audio.outputRoute == OutputRoute::MonitorOutput);
    assert(preset.voicebank.aliasStylePreference == AliasStylePreference::AutoDetect);
    assert(preset.voicebank.whistleAlias == "u");
    assert(!preset.recording.optInAutoCapture);
    assert(preset.recording.recordDryInput);
    assert(preset.recording.recordSynthOutput);
    assert(preset.recording.recordTimelineJson);
    assert(preset.latency.preset == LatencyPreset::Balanced);
    assert(preset.pitch.mode == PitchMode::FollowInput);
}

void rejectsRecordingFolderInsideRepository()
{
    auto preset = AppSettingsValidator::makeDefaultPreset();
    preset.recording.privateDataFolder = "/workspace/Recordings";

    const auto result = AppSettingsValidator::validate(preset, "/workspace");

    assert(!result.valid());
    assert(!result.errors.empty());
}

void allowsRecordingFolderOutsideRepository()
{
    auto preset = AppSettingsValidator::makeDefaultPreset();
    preset.recording.privateDataFolder = "/home/user/AppData/Local/Voice2VocalSynth";

    const auto result = AppSettingsValidator::validate(preset, "/workspace");

    assert(result.valid());
}

void warnsWhenVoicebankIsNotSelected()
{
    auto preset = AppSettingsValidator::makeDefaultPreset();
    preset.recording.privateDataFolder = "/home/user/AppData/Local/Voice2VocalSynth";

    const auto result = AppSettingsValidator::validate(preset, "/workspace");

    assert(result.valid());
    assert(!result.warnings.empty());
}

void detectsNestedPathsWithMixedSlashes()
{
    assert(AppSettingsValidator::pathIsInsideDirectory("C:\\project\\Recordings",
                                                       "C:/project"));
    assert(!AppSettingsValidator::pathIsInsideDirectory("C:/private/Recordings",
                                                        "C:/project"));
}

} // namespace

int main()
{
    defaultPresetUsesSafeLiveDefaults();
    rejectsRecordingFolderInsideRepository();
    allowsRecordingFolderOutsideRepository();
    warnsWhenVoicebankIsNotSelected();
    detectsNestedPathsWithMixedSlashes();

    std::cout << "AppSettings tests passed\n";
    return 0;
}
