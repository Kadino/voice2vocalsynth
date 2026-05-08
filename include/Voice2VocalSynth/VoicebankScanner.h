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
};

struct VoicebankScanResult
{
    std::filesystem::path rootPath;
    std::vector<std::filesystem::path> otoFiles;
    std::vector<OtoEntry> entries;
    std::vector<std::string> warnings;
    VoicebankAliasIndex aliasIndex;

    [[nodiscard]] VoicebankAliasStyle aliasStyle() const noexcept;
    [[nodiscard]] bool foundOtoIni() const noexcept;
};

class VoicebankScanner
{
public:
    [[nodiscard]] static VoicebankScanResult scan(const std::filesystem::path& rootPath,
                                                  const VoicebankScanOptions& options = {});

private:
    [[nodiscard]] static bool isOtoIniFile(const std::filesystem::path& path,
                                           const VoicebankScanOptions& options);
};

} // namespace Voice2VocalSynth
