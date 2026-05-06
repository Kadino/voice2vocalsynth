#include <Voice2VocalSynth/PhonemeFallbackMapper.h>

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace
{
using namespace Voice2VocalSynth;

void assertAliases(const std::vector<AliasEvent>& events, const std::vector<std::string>& aliases)
{
    assert(events.size() == aliases.size());

    for (std::size_t i = 0; i < aliases.size(); ++i)
    {
        assert(events[i].primaryAlias() == aliases[i]);
    }
}

void mapsFinalConsonantToShortPartialAlias()
{
    const PhonemeFallbackMapper mapper;
    const auto events = mapper.mapPhonemes({"K", "AE", "T"});

    assertAliases(events, {"ka", "to"});
    assert(events[0].role == AliasRole::CvSyllable);
    assert(events[1].role == AliasRole::PartialFinalConsonant);
    assert(events[1].isPartialFallback());
    assert(events[1].renderHint.preserveConsonantDuration);
    assert(events[1].renderHint.attenuateVowelTail);
    assert(events[1].renderHint.vowelTailGain < 0.25F);
    assert(events[1].renderHint.maxDurationMs <= 90.0);
}

void handlesClustersWithMultiplePartialFallbacks()
{
    const PhonemeFallbackMapper mapper;
    const auto events = mapper.mapPhonemes({"F", "AE", "S", "T"});

    assertAliases(events, {"fa", "su", "to"});
    assert(events[0].role == AliasRole::CvSyllable);
    assert(events[1].role == AliasRole::PartialFinalConsonant);
    assert(events[2].role == AliasRole::PartialFinalConsonant);
    assert(events[0].candidates.size() == 2);
    assert(events[0].candidates[1].alias == "ha");
}

void mapsRAndLToJapaneseRByDefault()
{
    const PhonemeFallbackMapper mapper;

    assertAliases(mapper.mapPhonemes({"R", "IY"}), {"ri"});
    assertAliases(mapper.mapPhonemes({"L", "IY"}), {"ri"});
}

void supportsConfigurableThFallback()
{
    auto options = PhonemeFallbackMapper::makeDefaultOptions();
    options.consonantSubstitutions["TH"] = "t";
    options.consonantFallbackCandidates["TH"] = {"s"};
    const PhonemeFallbackMapper mapper(std::move(options));

    const auto events = mapper.mapPhonemes({"TH", "AE"});

    assertAliases(events, {"ta"});
    assert(events[0].candidates.size() == 2);
    assert(events[0].candidates[0].alias == "ta");
    assert(events[0].candidates[1].alias == "sa");
}

void canDisablePartialFallback()
{
    auto options = PhonemeFallbackMapper::makeDefaultOptions();
    options.enablePartialCvcFallback = false;
    const PhonemeFallbackMapper mapper(std::move(options));

    const auto events = mapper.mapPhonemes({"K", "AE", "T"});

    assertAliases(events, {"ka"});
}

void stripsArpabetStressDigits()
{
    const PhonemeFallbackMapper mapper;

    assertAliases(mapper.mapPhonemes({"K", "AE1", "T"}), {"ka", "to"});
}

void mapsNasalFinalsToStandaloneN()
{
    const PhonemeFallbackMapper mapper;

    assertAliases(mapper.mapPhonemes({"S", "AO", "NG"}), {"so", "n"});
}

} // namespace

int main()
{
    mapsFinalConsonantToShortPartialAlias();
    handlesClustersWithMultiplePartialFallbacks();
    mapsRAndLToJapaneseRByDefault();
    supportsConfigurableThFallback();
    canDisablePartialFallback();
    stripsArpabetStressDigits();
    mapsNasalFinalsToStandaloneN();

    std::cout << "PhonemeFallbackMapper tests passed\n";
    return 0;
}
