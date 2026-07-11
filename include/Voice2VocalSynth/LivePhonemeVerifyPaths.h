#pragma once

#include <filesystem>
#include <string>

namespace Voice2VocalSynth
{

struct LivePhonemeVerifyLayout
{
    std::filesystem::path root;
    std::filesystem::path datasets;
    std::filesystem::path labels;
    std::filesystem::path runs;
};

struct LivePhonemeVerifyRunPaths
{
    std::filesystem::path runDirectory;
    std::filesystem::path liveLog;
    std::filesystem::path manifest;
    std::filesystem::path playbackManifest;
    std::filesystem::path predictions;
    std::filesystem::path metrics;
    std::filesystem::path report;
};

/// Default verification data root, e.g. `~/.local/share/Voice2VocalSynth/LivePhonemeVerify`.
[[nodiscard]] std::filesystem::path defaultLivePhonemeVerifyRoot();

[[nodiscard]] LivePhonemeVerifyLayout livePhonemeVerifyLayout(const std::filesystem::path& root);

/// Creates `datasets/`, `labels/`, and `runs/` under `root`.
[[nodiscard]] bool ensureLivePhonemeVerifyLayout(const std::filesystem::path& root, std::string& error);

/// UTC wall-clock timestamp for run directories, e.g. `20260707T182145Z`.
[[nodiscard]] std::string formatLivePhonemeVerifyRunTimestamp();

/// Creates `runs/<timestamp>/` with `live-log.jsonl` and `manifest.json` paths.
[[nodiscard]] bool createLivePhonemeVerifyRun(const std::filesystem::path& root,
                                              LivePhonemeVerifyRunPaths& out,
                                              std::string& error);

[[nodiscard]] LivePhonemeVerifyRunPaths livePhonemeVerifyRunPaths(
    const std::filesystem::path& runDirectory);

/// Writes the initial run manifest. `liveLogPath` is stored relative to `runDirectory` when possible.
[[nodiscard]] bool writeLivePhonemeVerifyManifest(const LivePhonemeVerifyRunPaths& paths,
                                                  std::string& error);

} // namespace Voice2VocalSynth
