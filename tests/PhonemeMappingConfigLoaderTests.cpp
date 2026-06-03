#include <Voice2VocalSynth/PhonemeMappingConfigLoader.h>

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{
using namespace Voice2VocalSynth;

void loads_repository_template()
{
    const auto path = PhonemeMappingConfigLoader::repositoryTemplatePath();
    assert(std::filesystem::exists(path));
    const auto loaded = PhonemeMappingConfigLoader::loadFromFile(path);
    assert(loaded.ok);
    assert(loaded.used_file);

    const PhonemeFallbackMapper mapper(loaded.options);
    const auto events = mapper.mapPhonemes({"K", "AE", "T"});
    assert(events.size() == 2);
    assert(events[0].primaryAlias() == "ka");
    assert(events[1].primaryAlias() == "to");
}

void partial_override_changes_th_mapping()
{
#ifndef VOICE2VOCALSYNTH_REPOSITORY_ROOT
    return;
#endif
    const auto path = std::filesystem::path(VOICE2VOCALSYNTH_REPOSITORY_ROOT) /
                      "tests/fixtures/phoneme_mapping_th_primary.json";
    const auto loaded = PhonemeMappingConfigLoader::loadFromFile(path);
    assert(loaded.ok);

    const PhonemeFallbackMapper mapper(loaded.options);
    const auto events = mapper.mapPhonemes({"TH", "AE"});
    assert(events.size() == 1);
    assert(events[0].primaryAlias() == "ta");
    assert(events[0].candidates.size() == 2);
    assert(events[0].candidates[1].alias == "sa");
}

void invalid_json_falls_back_with_error()
{
    const auto loaded = PhonemeMappingConfigLoader::loadFromJson("{ not json");
    assert(!loaded.ok);
    assert(!loaded.error.empty());
}

void merge_with_defaults_keeps_unlisted_phonemes()
{
    PhonemeFallbackOptions partial;
    partial.consonantSubstitutions["TH"] = "t";
    partial.consonantFallbackCandidates["TH"] = {"s"};
    const auto merged = PhonemeMappingConfigLoader::mergeWithDefaults(partial);

    const PhonemeFallbackMapper mapper(merged);
    assert(mapper.mapPhonemes({"TH", "AE"})[0].primaryAlias() == "ta");
    assert(mapper.mapPhonemes({"K", "AE"})[0].primaryAlias() == "ka");
}

} // namespace

int main()
{
    loads_repository_template();
    partial_override_changes_th_mapping();
    invalid_json_falls_back_with_error();
    merge_with_defaults_keeps_unlisted_phonemes();
    std::cout << "PhonemeMappingConfigLoader tests passed\n";
    return 0;
}
