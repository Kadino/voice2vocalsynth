#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Voice2VocalSynth
{

struct EvalDataLayout
{
    std::filesystem::path root;
    std::filesystem::path recordings;
    std::filesystem::path labels;
    std::filesystem::path predictions;
    std::filesystem::path readme;
};

/// Default private evaluation root, e.g. `%LOCALAPPDATA%/Voice2VocalSynth/EvalData`.
[[nodiscard]] std::filesystem::path defaultEvalDataRoot();

[[nodiscard]] EvalDataLayout evalDataLayout(const std::filesystem::path& root);

/// Creates `recordings/`, `labels/`, and `predictions/` under `root`.
[[nodiscard]] bool ensureEvalDataLayout(const std::filesystem::path& root, std::string& error);

/// Writes a short setup note into the eval root when missing.
[[nodiscard]] bool ensureEvalDataReadme(const std::filesystem::path& root, std::string& error);

/// Lists clip basenames that have both `recordings/<name>.wav` and `labels/<name>.json`.
[[nodiscard]] std::vector<std::string> listEvalClipNames(const std::filesystem::path& evalRoot);

} // namespace Voice2VocalSynth
