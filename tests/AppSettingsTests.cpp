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

void detectsNestedPathsWithMixedSlashes()
{
    assert(AppSettingsValidator::pathIsInsideDirectory("C:\\project\\Recordings", "C:/project"));
    assert(!AppSettingsValidator::pathIsInsideDirectory("C:/private/Recordings", "C:/project"));
}

void writesAndReadsEditableJson()
{
    auto preset = AppSettingsValidator::makeDefaultPreset();
    preset.name = "Live test";
    preset.audio.inputDeviceName = "Mic";
    preset.audio.outputDeviceName = "Headphones";
    preset.voicebank.voicebankPath = "C:/voicebanks/momo";
    preset.voicebank.whistleAlias = "u";
    preset.pitch.mode = PitchMode::SnapToKey;
    preset.pitch.scale = ScaleType::NaturalMinor;
    preset.pitch.keyRootPitchClass = 9;
    preset.pitch.octaveShift = 1;
    preset.pitch.defaultFrequencyHz = PitchTargetCalculator::midiToFrequency(62.0);
    preset.latency = LatencyBudgetCalculator::presetSettings(LatencyPreset::HighAccuracy);
    preset.recording.privateDataFolder = "C:/Users/me/AppData/Local/Voice2VocalSynth";

    const auto json = AppPresetJson::toJson(preset);
    assert(json.find("\"voicebankPath\"") != std::string::npos);
    assert(json.find("\"snapToKey\"") != std::string::npos);

    const auto parsed = AppPresetJson::fromJson(json);
    assert(parsed.name == "Live test");
    assert(parsed.audio.inputDeviceName == "Mic");
    assert(parsed.voicebank.voicebankPath == "C:/voicebanks/momo");
    assert(parsed.pitch.mode == PitchMode::SnapToKey);
    assert(parsed.pitch.scale == ScaleType::NaturalMinor);
    assert(parsed.pitch.keyRootPitchClass == 9);
    assert(parsed.pitch.octaveShift == 1);
    assert(parsed.latency.preset == LatencyPreset::HighAccuracy);
    assert(parsed.recording.privateDataFolder == "C:/Users/me/AppData/Local/Voice2VocalSynth");
}

void acceptsHumanEditedPartialJson()
{
    const auto parsed = AppPresetJson::fromJson(
        "{\n"
        "  \"name\": \"Edited\",\n"
        "  \"voicebank\": { \"whistleAlias\": \"o\" },\n"
        "  \"pitch\": { \"mode\": \"fixedDefault\", \"defaultFrequencyHz\": 3.3e2 },\n"
        "  \"recording\": { \"optInAutoCapture\": true, \"privateDataFolder\": \"D:/captures\" }\n"
        "}\n");

    assert(parsed.name == "Edited");
    assert(parsed.voicebank.whistleAlias == "o");
    assert(parsed.pitch.mode == PitchMode::FixedDefault);
    assert(parsed.pitch.defaultFrequencyHz == 330.0);
    assert(parsed.recording.optInAutoCapture);
    assert(parsed.recording.privateDataFolder == "D:/captures");
}

} // namespace

int main()
{
    defaultPresetUsesSafeLiveDefaults();
    rejectsRecordingFolderInsideRepository();
    allowsRecordingFolderOutsideRepository();
    detectsNestedPathsWithMixedSlashes();
    writesAndReadsEditableJson();
    acceptsHumanEditedPartialJson();

    std::cout << "AppSettings tests passed\n";
    return 0;
}
