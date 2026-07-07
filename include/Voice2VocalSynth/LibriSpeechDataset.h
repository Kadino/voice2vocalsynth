#pragma once

#include "Voice2VocalSynth/LivePhonemeVerifyPaths.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Voice2VocalSynth
{

inline constexpr std::string_view kLibriSpeechTestCleanArchiveUrl =
    "https://www.openslr.org/resources/12/test-clean.tar.gz";

inline constexpr std::string_view kLibriSpeechTestCleanRelativePath = "LibriSpeech/test-clean";

struct LibriSpeechTestCleanSummary
{
    std::filesystem::path root;
    std::size_t speakerCount = 0;
    std::size_t chapterCount = 0;
    std::size_t flacCount = 0;
    std::size_t transcriptFileCount = 0;
};

struct LibriSpeechTestCleanValidation
{
    bool valid = false;
    LibriSpeechTestCleanSummary summary;
    std::string error;
};

/// Canonical dataset root under a verification root, e.g. `.../datasets/LibriSpeech/test-clean`.
[[nodiscard]] std::filesystem::path defaultLibriSpeechTestCleanRoot(
    const std::filesystem::path& verifyRoot = defaultLivePhonemeVerifyRoot());

/// Default manifest path beside the dataset root.
[[nodiscard]] std::filesystem::path defaultLibriSpeechDatasetManifestPath(
    const std::filesystem::path& verifyRoot = defaultLivePhonemeVerifyRoot());

/// Validates LibriSpeech `test-clean` layout (speaker/chapter folders, FLAC + transcript files).
[[nodiscard]] LibriSpeechTestCleanValidation validateLibriSpeechTestClean(
    const std::filesystem::path& root);

/// Resolves an existing dataset root from env override or default verification layout.
[[nodiscard]] std::optional<std::filesystem::path> discoverLibriSpeechTestCleanRoot(
    const std::filesystem::path& verifyRoot,
    std::string& note);

[[nodiscard]] bool writeLibriSpeechDatasetManifest(const LibriSpeechTestCleanSummary& summary,
                                                   const std::filesystem::path& manifestPath,
                                                   std::string& error);

} // namespace Voice2VocalSynth
