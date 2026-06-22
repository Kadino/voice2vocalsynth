#include "Voice2VocalSynth/EvalDataPaths.h"

#include <cstdlib>
#include <fstream>
#include <algorithm>
#include <vector>

namespace Voice2VocalSynth
{

std::filesystem::path defaultEvalDataRoot()
{
#if defined(_WIN32)
    if (const char* localAppData = std::getenv("LOCALAPPDATA")) {
        return std::filesystem::path(localAppData) / "Voice2VocalSynth" / "EvalData";
    }
#endif
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".local" / "share" / "Voice2VocalSynth" / "EvalData";
    }
    return std::filesystem::path("EvalData");
}

EvalDataLayout evalDataLayout(const std::filesystem::path& root)
{
    EvalDataLayout layout;
    layout.root = root;
    layout.recordings = root / "recordings";
    layout.labels = root / "labels";
    layout.predictions = root / "predictions";
    layout.readme = root / "README.txt";
    return layout;
}

bool ensureEvalDataLayout(const std::filesystem::path& root, std::string& error)
{
    error.clear();
    const auto layout = evalDataLayout(root);
    std::error_code ec;
    if (!std::filesystem::create_directories(layout.recordings, ec) && !std::filesystem::exists(layout.recordings)) {
        error = "Unable to create recordings directory: " + layout.recordings.string();
        return false;
    }
    if (!std::filesystem::create_directories(layout.labels, ec) && !std::filesystem::exists(layout.labels)) {
        error = "Unable to create labels directory: " + layout.labels.string();
        return false;
    }
    if (!std::filesystem::create_directories(layout.predictions, ec) && !std::filesystem::exists(layout.predictions)) {
        error = "Unable to create predictions directory: " + layout.predictions.string();
        return false;
    }
    return ensureEvalDataReadme(root, error);
}

bool ensureEvalDataReadme(const std::filesystem::path& root, std::string& error)
{
    error.clear();
    const auto layout = evalDataLayout(root);
    if (std::filesystem::exists(layout.readme)) {
        return true;
    }

    std::ofstream output(layout.readme, std::ios::binary);
    if (!output) {
        error = "Unable to write eval data README: " + layout.readme.string();
        return false;
    }

    output << "Voice2VocalSynth private evaluation data\n";
    output << "=======================================\n\n";
    output << "Do not commit this folder to Git.\n\n";
    output << "Layout:\n";
    output << "  recordings/  WAV clips (spoken, sung, whispered, plosives, etc.)\n";
    output << "  labels/      JSON frame labels accepted by loadPhonemeFrameLabelsJson\n";
    output << "  predictions/ optional backend prediction JSON exports\n\n";
    output << "Suggested first clips:\n";
    output << "  - vowels (spoken, sung, whispered)\n";
    output << "  - plosives P T K B D G\n";
    output << "  - fricatives S SH F TH V Z\n";
    output << "  - nasals M N NG\n";
    output << "  - R L W Y, nonsense syllables, whistle examples\n";
    return true;
}

std::vector<std::string> listEvalClipNames(const std::filesystem::path& evalRoot)
{
    std::vector<std::string> clips;
    const auto layout = evalDataLayout(evalRoot);
    if (!std::filesystem::is_directory(layout.recordings) || !std::filesystem::is_directory(layout.labels)) {
        return clips;
    }

    for (const auto& entry : std::filesystem::directory_iterator(layout.recordings)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".wav") {
            continue;
        }
        const auto clipName = entry.path().stem().string();
        if (std::filesystem::exists(layout.labels / (clipName + ".json"))) {
            clips.push_back(clipName);
        }
    }

    std::sort(clips.begin(), clips.end());
    return clips;
}

} // namespace Voice2VocalSynth
