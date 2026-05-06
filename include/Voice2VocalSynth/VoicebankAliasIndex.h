#pragma once

#include "Voice2VocalSynth/PhonemeFallbackMapper.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Voice2VocalSynth
{

enum class VoicebankAliasStyle
{
    Empty,
    Romaji,
    NonAscii,
    Mixed
};

struct OtoEntry
{
    std::string wavFile;
    std::string alias;
    std::string sourceName;
    double offsetMs = 0.0;
    double consonantMs = 0.0;
    double cutoffMs = 0.0;
    double preutteranceMs = 0.0;
    double overlapMs = 0.0;
    std::size_t sourceLine = 0;
};

struct AliasResolution
{
    bool resolved = false;
    std::string selectedAlias;
    std::size_t candidateIndex = 0;
    std::string reason;
    bool usedPartialFallback = false;
    AliasRole role = AliasRole::Unknown;
    std::vector<std::string> sourcePhonemes;
    std::vector<std::string> attemptedAliases;
    std::vector<std::string> missingCandidates;
    const OtoEntry* entry = nullptr;
};

class VoicebankAliasIndex
{
public:
    void addEntry(OtoEntry entry);
    void addAlias(std::string alias);

    [[nodiscard]] static VoicebankAliasIndex fromEntries(const std::vector<OtoEntry>& entries);

    [[nodiscard]] bool containsAlias(std::string_view alias) const;
    [[nodiscard]] const OtoEntry* findFirst(std::string_view alias) const;
    [[nodiscard]] AliasResolution resolve(const AliasEvent& event) const;
    [[nodiscard]] std::vector<AliasResolution> resolveAll(const std::vector<AliasEvent>& events) const;
    [[nodiscard]] VoicebankAliasStyle aliasStyle() const noexcept;
    [[nodiscard]] VoicebankAliasStyle detectAliasStyle() const noexcept;
    [[nodiscard]] std::size_t aliasCount() const noexcept;
    [[nodiscard]] std::size_t entryCount() const noexcept;
    [[nodiscard]] std::vector<std::string> aliases() const;

private:
    std::unordered_map<std::string, std::vector<OtoEntry>> entriesByAlias_;
};

[[nodiscard]] std::optional<OtoEntry> parseOtoIniLine(std::string_view line,
                                                      std::size_t sourceLine,
                                                      std::string sourceName = "oto.ini");
[[nodiscard]] std::vector<OtoEntry> parseOtoIniContent(std::string_view content,
                                                       std::string sourceName = "oto.ini");
[[nodiscard]] VoicebankAliasIndex buildVoicebankAliasIndex(const std::vector<OtoEntry>& entries);

} // namespace Voice2VocalSynth
