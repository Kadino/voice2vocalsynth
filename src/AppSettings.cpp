#include "Voice2VocalSynth/AppSettings.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace Voice2VocalSynth
{
namespace
{

std::string trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string normalizePathForWindowsComparison(std::string value)
{
    std::replace(value.begin(), value.end(), '\\', '/');

    while (value.size() > 1 && value.back() == '/') {
        value.pop_back();
    }

    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    return value;
}

void addWarningIfEmpty(std::vector<std::string>& warnings,
                       const std::string& value,
                       const char* message)
{
    if (trim(value).empty()) {
        warnings.emplace_back(message);
    }
}

} // namespace

bool AppSettingsValidation::valid() const noexcept
{
    return errors.empty();
}

AppPreset AppSettingsValidator::makeDefaultPreset()
{
    AppPreset preset;
    preset.name = "Default";
    preset.audio.outputRoute = OutputRoute::MonitorOutput;
    preset.voicebank.aliasStylePreference = AliasStylePreference::AutoDetect;
    preset.voicebank.allowMissingAliasFallback = true;
    preset.voicebank.whistleAlias = "u";
    preset.latency = LatencyBudgetCalculator::presetSettings(LatencyPreset::Balanced);
    preset.pitch = PitchTargetOptions{};
    preset.recording.optInAutoCapture = false;
    return preset;
}

AppSettingsValidation AppSettingsValidator::validate(const AppPreset& preset,
                                                     const std::string& repositoryRoot)
{
    AppSettingsValidation validation;

    if (trim(preset.name).empty()) {
        validation.errors.emplace_back("Preset name must not be empty");
    }

    addWarningIfEmpty(validation.warnings,
                      preset.audio.inputDeviceName,
                      "Input device has not been selected");
    addWarningIfEmpty(validation.warnings,
                      preset.audio.outputDeviceName,
                      "Output device has not been selected");
    addWarningIfEmpty(validation.warnings,
                      preset.voicebank.voicebankPath,
                      "Voicebank folder has not been selected");

    if (trim(preset.voicebank.whistleAlias).empty()) {
        validation.errors.emplace_back("Whistle alias must not be empty");
    }

    if (preset.pitch.defaultFrequencyHz <= 0.0) {
        validation.errors.emplace_back("Default pitch frequency must be positive");
    }

    if (preset.recording.optInAutoCapture && trim(preset.recording.privateDataFolder).empty()) {
        validation.errors.emplace_back("Auto-capture requires a private data folder");
    }

    if (!trim(repositoryRoot).empty() && !trim(preset.recording.privateDataFolder).empty() &&
        pathIsInsideDirectory(preset.recording.privateDataFolder, repositoryRoot)) {
        validation.errors.emplace_back("Private recording data folder must not be inside the Git repository");
    }

    if (trim(preset.recording.privateDataFolder).empty()) {
        validation.warnings.emplace_back("Private recording data folder has not been selected");
    }

    return validation;
}

bool AppSettingsValidator::pathIsInsideDirectory(const std::string& path,
                                                 const std::string& directory)
{
    auto child = normalizePathForWindowsComparison(trim(path));
    auto parent = normalizePathForWindowsComparison(trim(directory));

    if (child.empty() || parent.empty()) {
        return false;
    }

    if (child == parent) {
        return true;
    }

    if (parent.back() != '/') {
        parent.push_back('/');
    }

    return child.rfind(parent, 0) == 0;
}

} // namespace Voice2VocalSynth
