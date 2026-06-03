#pragma once

#include "Voice2VocalSynth/PhonemeFallbackMapper.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Voice2VocalSynth
{

struct PhonemeMappingLoadResult
{
    bool ok = false;
    std::string error;
    PhonemeFallbackOptions options = PhonemeFallbackMapper::makeDefaultOptions();
    std::vector<std::string> warnings;
    bool used_file = false;
};

/// Loads `phonemeToJapaneseMapping` from JSON (`voice2vocalsynth.spec.md` exampleConfig shape).
class PhonemeMappingConfigLoader
{
public:
    [[nodiscard]] static PhonemeMappingLoadResult loadFromFile(const std::filesystem::path& path);

    /// Parses JSON text; on failure returns defaults in `options` with `ok == false`.
    [[nodiscard]] static PhonemeMappingLoadResult loadFromJson(std::string_view json);

    /// User file when present, otherwise built-in defaults (`phonemeToJapaneseMapping.mustLiveInConfigFile`).
    [[nodiscard]] static PhonemeMappingLoadResult loadEffective(
        const std::optional<std::filesystem::path>& explicit_path = std::nullopt);

    [[nodiscard]] static std::filesystem::path defaultUserConfigPath();

    [[nodiscard]] static std::filesystem::path repositoryTemplatePath();

    [[nodiscard]] static PhonemeFallbackOptions mergeWithDefaults(const PhonemeFallbackOptions& from_file);

    [[nodiscard]] static std::string defaultConfigJson();
};

} // namespace Voice2VocalSynth
