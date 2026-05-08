#pragma once

#include "Voice2VocalSynth/VoicebankAliasIndex.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Voice2VocalSynth
{

struct VoicebankScanOptions
{
    bool recursive = true;
    bool caseInsensitiveOtoFileName = true;
    std::string otoFileName = "oto.ini";
    std::string prefixMapFileName = "prefix.map";
};

struct VoicebankPrefixMapEntry
{
    std::string noteName;
    std::string prefix;
    std::string suffix;
    std::string sourceName;
    std::size_t sourceLine = 0;
};

struct VoicebankScanResult
{
    std::filesystem::path rootPath;
    std::vector<std::filesystem::path> otoFiles;
    std::vector<std::filesystem::path> prefixMapFiles;
    std::vector<OtoEntry> entries;
    std::vector<VoicebankPrefixMapEntry> prefixMapEntries;
    std::vector<std::string> warnings;
    VoicebankAliasIndex aliasIndex;

    [[nodiscard]] VoicebankAliasStyle aliasStyle() const noexcept;
    [[nodiscard]] bool foundOtoIni() const noexcept;
    [[nodiscard]] bool foundPrefixMap() const noexcept;
};

struct VoicebankPrefixMapMatch
{
    bool found = false;
    VoicebankPrefixMapEntry entry;
};

class VoicebankScanner
{
public:
    [[nodiscard]] static VoicebankScanResult scan(const std::filesystem::path& rootPath,
                                                  const VoicebankScanOptions& options = {});

private:
    [[nodiscard]] static bool matchesConfiguredFileName(const std::filesystem::path& path,
                                                        const std::string& expectedFileName,
                                                        bool caseInsensitive);
};

[[nodiscard]] std::vector<VoicebankPrefixMapEntry> parsePrefixMapContent(
    std::string_view content,
    std::string sourceName = "prefix.map");
[[nodiscard]] VoicebankPrefixMapMatch findPrefixMapEntry(
    const std::vector<VoicebankPrefixMapEntry>& entries,
    std::string_view noteName);
[[nodiscard]] AliasEvent applyPrefixMapToAliasEvent(
    const AliasEvent& event,
    const std::vector<VoicebankPrefixMapEntry>& entries,
    std::string_view noteName);

} // namespace Voice2VocalSynth
