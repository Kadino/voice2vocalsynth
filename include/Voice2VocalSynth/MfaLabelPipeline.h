#pragma once

#include "Voice2VocalSynth/LibriSpeechDataset.h"
#include "Voice2VocalSynth/PhonemeFrame.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Voice2VocalSynth
{

struct MfaTextGridParseResult
{
    bool ok = false;
    std::string error;
    std::vector<PhonemeFrame> frames;
};

struct MfaLabelManifestInfo
{
    std::string mfaVersion;
    std::string acousticModel = std::string(kMfaEnglishUsArpaAcousticModel);
    std::string dictionary = std::string(kMfaEnglishUsArpaDictionary);
    std::string subsetMode;
    std::size_t utteranceCount = 0;
    std::size_t labelFileCount = 0;
    std::filesystem::path labelsDirectory;
    std::filesystem::path datasetRoot;
};

/// Strips MFA stress digits, e.g. `AH0` -> `AH`.
[[nodiscard]] std::string stripMfaArpabetStress(std::string_view label);

[[nodiscard]] bool isSkippableMfaPhoneLabel(std::string_view label);

[[nodiscard]] MfaTextGridParseResult parseMfaPhonesTextGrid(std::string_view textGrid);

[[nodiscard]] std::string phonemeFramesToLabelJson(const std::vector<PhonemeFrame>& frames);

[[nodiscard]] bool writePhonemeLabelJsonFile(const std::filesystem::path& path,
                                             const std::vector<PhonemeFrame>& frames,
                                             std::string& error);

struct MfaTextGridConversionSummary
{
    std::size_t textGridCount = 0;
    std::size_t labelFileCount = 0;
    std::size_t skippedTextGrids = 0;
};

[[nodiscard]] bool convertMfaTextGridTree(const std::filesystem::path& textGridRoot,
                                          const std::filesystem::path& labelsRoot,
                                          MfaTextGridConversionSummary& summary,
                                          std::string& error);

[[nodiscard]] bool writeMfaLabelManifest(const MfaLabelManifestInfo& info,
                                         const std::filesystem::path& manifestPath,
                                         std::string& error);

} // namespace Voice2VocalSynth
