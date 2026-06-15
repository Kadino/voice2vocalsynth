#pragma once

#include <filesystem>
#include <string>

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

} // namespace Voice2VocalSynth
