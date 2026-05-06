#include "Voice2VocalSynth/PhonemeFallbackMapper.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace Voice2VocalSynth
{
namespace
{

std::string upperAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return value;
}

std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool containsAlias(const std::vector<AliasCandidate>& candidates, const std::string& alias)
{
    return std::any_of(candidates.begin(), candidates.end(), [&alias](const AliasCandidate& candidate) {
        return candidate.alias == alias;
    });
}

void appendFallbackCandidate(std::vector<AliasCandidate>& candidates, std::string alias)
{
    if (alias.empty() || containsAlias(candidates, alias)) {
        return;
    }

    candidates.push_back({std::move(alias), "fallback"});
}

} // namespace

const std::string& AliasEvent::primaryAlias() const
{
    static const std::string empty;
    return candidates.empty() ? empty : candidates.front().alias;
}

bool AliasEvent::isPartialFallback() const noexcept
{
    return role == AliasRole::PartialFinalConsonant;
}

PhonemeFallbackMapper::PhonemeFallbackMapper()
    : PhonemeFallbackMapper(makeDefaultOptions())
{
}

PhonemeFallbackMapper::PhonemeFallbackMapper(PhonemeFallbackOptions options)
    : options_(std::move(options))
{
}

std::vector<AliasEvent> PhonemeFallbackMapper::mapPhonemes(
    const std::vector<std::string>& arpabetPhonemes) const
{
    std::vector<std::string> normalized;
    normalized.reserve(arpabetPhonemes.size());

    for (const auto& phoneme : arpabetPhonemes) {
        auto value = normalizeArpabet(phoneme);
        if (!value.empty()) {
            normalized.push_back(std::move(value));
        }
    }

    std::vector<AliasEvent> events;

    for (std::size_t index = 0; index < normalized.size();) {
        const auto& current = normalized[index];

        if (isVowel(current)) {
            events.push_back(makeVowelEvent(current));
            ++index;
            continue;
        }

        if (isConsonant(current)) {
            const auto hasFollowingVowel =
                index + 1 < normalized.size() && isVowel(normalized[index + 1]);

            if (hasFollowingVowel) {
                events.push_back(makeCvEvent(current, normalized[index + 1]));
                index += 2;
                continue;
            }

            if (options_.enablePartialCvcFallback) {
                events.push_back(makePartialFinalEvent(current));
            }

            ++index;
            continue;
        }

        ++index;
    }

    return events;
}

const PhonemeFallbackOptions& PhonemeFallbackMapper::options() const noexcept
{
    return options_;
}

PhonemeFallbackOptions PhonemeFallbackMapper::makeDefaultOptions()
{
    PhonemeFallbackOptions options;

    options.vowelSubstitutions = {
        {"AA", "a"}, {"AE", "a"}, {"AH", "a"}, {"AO", "o"}, {"AW", "a"},
        {"AY", "a"}, {"EH", "e"}, {"ER", "a"}, {"EY", "e"}, {"IH", "i"},
        {"IY", "i"}, {"OW", "o"}, {"OY", "o"}, {"UH", "u"}, {"UW", "u"},
    };

    options.consonantSubstitutions = {
        {"B", "b"},   {"CH", "ch"}, {"D", "d"},  {"DH", "z"}, {"F", "f"},
        {"G", "g"},   {"HH", "h"},  {"JH", "j"}, {"K", "k"},  {"L", "r"},
        {"M", "m"},   {"N", "n"},   {"NG", "n"}, {"P", "p"},  {"R", "r"},
        {"S", "s"},   {"SH", "sh"}, {"T", "t"},  {"TH", "s"}, {"V", "b"},
        {"W", "w"},   {"Y", "y"},   {"Z", "z"},  {"ZH", "j"},
    };

    options.consonantFallbackCandidates = {
        {"DH", {"d"}},
        {"F", {"h"}},
        {"TH", {"t"}},
        {"V", {"f", "b"}},
        {"ZH", {"j"}},
    };

    options.finalConsonantAliasOverrides = {
        {"N", "n"},
        {"NG", "n"},
    };

    options.finalConsonantVowelOverrides = {
        {"D", "o"},
        {"T", "o"},
    };

    return options;
}

std::string PhonemeFallbackMapper::normalizeArpabet(std::string phoneme)
{
    phoneme = upperAscii(std::move(phoneme));

    while (!phoneme.empty() && std::isdigit(static_cast<unsigned char>(phoneme.back()))) {
        phoneme.pop_back();
    }

    return phoneme;
}

bool PhonemeFallbackMapper::isArpabetVowel(const std::string& phoneme)
{
    const auto normalized = normalizeArpabet(phoneme);
    const auto defaults = makeDefaultOptions();
    return defaults.vowelSubstitutions.find(normalized) != defaults.vowelSubstitutions.end();
}

AliasEvent PhonemeFallbackMapper::makeVowelEvent(const std::string& vowel) const
{
    AliasEvent event;
    event.role = AliasRole::Vowel;
    event.sourcePhonemes = {vowel};
    event.candidates = {{mapVowel(vowel), "primary"}};
    return event;
}

AliasEvent PhonemeFallbackMapper::makeCvEvent(const std::string& consonant,
                                              const std::string& vowel) const
{
    const auto mappedVowel = mapVowel(vowel);
    std::vector<AliasCandidate> candidates = {
        {buildCvAlias(mapConsonant(consonant), mappedVowel), "primary"},
    };

    const auto fallbackBases = options_.consonantFallbackCandidates.find(consonant);
    if (fallbackBases != options_.consonantFallbackCandidates.end()) {
        for (const auto& fallbackBase : fallbackBases->second) {
            appendFallbackCandidate(candidates, buildCvAlias(fallbackBase, mappedVowel));
        }
    }

    AliasEvent event;
    event.role = AliasRole::CvSyllable;
    event.sourcePhonemes = {consonant, vowel};
    event.candidates = std::move(candidates);
    event.renderHint.preserveConsonantDuration = true;
    return event;
}

AliasEvent PhonemeFallbackMapper::makePartialFinalEvent(const std::string& consonant) const
{
    std::vector<AliasCandidate> candidates;

    const auto aliasOverride = options_.finalConsonantAliasOverrides.find(consonant);
    if (aliasOverride != options_.finalConsonantAliasOverrides.end()) {
        candidates.push_back({aliasOverride->second, "primary"});
    } else {
        auto helperVowel = options_.defaultFinalConsonantVowel;
        const auto vowelOverride = options_.finalConsonantVowelOverrides.find(consonant);
        if (vowelOverride != options_.finalConsonantVowelOverrides.end()) {
            helperVowel = vowelOverride->second;
        }

        candidates.push_back({buildCvAlias(mapConsonant(consonant), helperVowel), "primary"});
    }

    const auto fallbackBases = options_.consonantFallbackCandidates.find(consonant);
    if (fallbackBases != options_.consonantFallbackCandidates.end()) {
        for (const auto& fallbackBase : fallbackBases->second) {
            appendFallbackCandidate(candidates, buildCvAlias(fallbackBase, options_.defaultFinalConsonantVowel));
        }
    }

    AliasEvent event;
    event.role = AliasRole::PartialFinalConsonant;
    event.sourcePhonemes = {consonant};
    event.candidates = std::move(candidates);
    event.renderHint.preserveConsonantDuration = true;
    event.renderHint.attenuateVowelTail = true;
    event.renderHint.vowelTailGain = options_.partialFinalVowelTailGain;
    event.renderHint.maxDurationMs = options_.partialFinalMaxDurationMs;
    return event;
}

std::string PhonemeFallbackMapper::mapVowel(const std::string& vowel) const
{
    const auto found = options_.vowelSubstitutions.find(vowel);
    if (found == options_.vowelSubstitutions.end()) {
        throw std::invalid_argument("Missing vowel substitution for ARPABET phoneme " + vowel);
    }

    return found->second;
}

std::string PhonemeFallbackMapper::mapConsonant(const std::string& consonant) const
{
    const auto found = options_.consonantSubstitutions.find(consonant);
    if (found == options_.consonantSubstitutions.end()) {
        throw std::invalid_argument("Missing consonant substitution for ARPABET phoneme " + consonant);
    }

    return found->second;
}

bool PhonemeFallbackMapper::isVowel(const std::string& normalizedPhoneme) const
{
    return options_.vowelSubstitutions.find(normalizedPhoneme) != options_.vowelSubstitutions.end();
}

bool PhonemeFallbackMapper::isConsonant(const std::string& normalizedPhoneme) const
{
    return options_.consonantSubstitutions.find(normalizedPhoneme) !=
           options_.consonantSubstitutions.end();
}

std::string PhonemeFallbackMapper::buildCvAlias(const std::string& consonantBase,
                                                const std::string& japaneseVowel) const
{
    const auto consonant = lowerAscii(consonantBase);
    const auto vowel = lowerAscii(japaneseVowel);

    if (consonant.empty()) {
        return vowel;
    }

    if (consonant == "s" && vowel == "i") {
        return "shi";
    }
    if (consonant == "t" && vowel == "i") {
        return "chi";
    }
    if (consonant == "t" && vowel == "u") {
        return "tsu";
    }
    if (consonant == "ch" && vowel == "i") {
        return "chi";
    }
    if (consonant == "j" && vowel == "i") {
        return "ji";
    }
    if (consonant == "z" && vowel == "i") {
        return "ji";
    }
    if (consonant == "sh" && vowel == "i") {
        return "shi";
    }
    if (consonant == "f" && vowel == "u") {
        return "fu";
    }
    if (consonant == "y" && vowel == "i") {
        return "i";
    }
    if (consonant == "w" && vowel == "u") {
        return "u";
    }

    return consonant + vowel;
}

} // namespace Voice2VocalSynth
