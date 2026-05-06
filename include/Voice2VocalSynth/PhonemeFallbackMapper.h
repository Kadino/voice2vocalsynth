#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace Voice2VocalSynth
{

enum class AliasRole
{
    Vowel,
    CvSyllable,
    PartialFinalConsonant,
    Unknown
};

struct RenderHint
{
    bool preserveConsonantDuration = false;
    bool attenuateVowelTail = false;
    float vowelTailGain = 1.0F;
    double maxDurationMs = 0.0;
};

struct AliasCandidate
{
    std::string alias;
    std::string reason;
};

struct AliasEvent
{
    AliasRole role = AliasRole::Unknown;
    std::vector<std::string> sourcePhonemes;
    std::vector<AliasCandidate> candidates;
    RenderHint renderHint;

    [[nodiscard]] const std::string& primaryAlias() const;
    [[nodiscard]] bool isPartialFallback() const noexcept;
};

struct PhonemeFallbackOptions
{
    bool enablePartialCvcFallback = true;

    // Used when a final consonant has no specific vowel override.
    std::string defaultFinalConsonantVowel = "u";

    // Maps ARPABET vowels/diphthongs onto Japanese vowels.
    std::unordered_map<std::string, std::string> vowelSubstitutions;

    // Maps ARPABET consonants onto the closest Japanese CV consonant base.
    std::unordered_map<std::string, std::string> consonantSubstitutions;

    // Secondary consonant bases to try when the primary alias is missing.
    std::unordered_map<std::string, std::vector<std::string>> consonantFallbackCandidates;

    // Overrides the helper vowel for final consonants, e.g. T -> o gives "to".
    std::unordered_map<std::string, std::string> finalConsonantVowelOverrides;

    // Overrides the whole final consonant alias, e.g. N -> n.
    std::unordered_map<std::string, std::string> finalConsonantAliasOverrides;

    float partialFinalVowelTailGain = 0.15F;
    double partialFinalMaxDurationMs = 90.0;
};

class PhonemeFallbackMapper
{
public:
    PhonemeFallbackMapper();
    explicit PhonemeFallbackMapper(PhonemeFallbackOptions options);

    [[nodiscard]] std::vector<AliasEvent> mapPhonemes(const std::vector<std::string>& arpabetPhonemes) const;
    [[nodiscard]] const PhonemeFallbackOptions& options() const noexcept;

    [[nodiscard]] static PhonemeFallbackOptions makeDefaultOptions();
    [[nodiscard]] static std::string normalizeArpabet(std::string phoneme);
    [[nodiscard]] static bool isArpabetVowel(const std::string& normalizedPhoneme);

private:
    [[nodiscard]] AliasEvent makeVowelEvent(const std::string& vowel) const;
    [[nodiscard]] AliasEvent makeCvEvent(const std::string& consonant, const std::string& vowel) const;
    [[nodiscard]] AliasEvent makePartialFinalEvent(const std::string& consonant) const;

    [[nodiscard]] std::string mapVowel(const std::string& vowel) const;
    [[nodiscard]] std::string mapConsonant(const std::string& consonant) const;
    [[nodiscard]] bool isVowel(const std::string& normalizedPhoneme) const;
    [[nodiscard]] bool isConsonant(const std::string& normalizedPhoneme) const;
    [[nodiscard]] std::string buildCvAlias(const std::string& consonantBase, const std::string& japaneseVowel) const;

    PhonemeFallbackOptions options_;
};

} // namespace Voice2VocalSynth
