#include <Voice2VocalSynth/EvalDataPaths.h>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
using namespace Voice2VocalSynth;

std::filesystem::path tempEvalRoot()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("voice2vocalsynth-eval-data-" + std::to_string(stamp));
}

void createsEvalDataLayout()
{
    const auto root = tempEvalRoot();
    std::string error;
    assert(ensureEvalDataLayout(root, error));
    assert(error.empty());

    const auto layout = evalDataLayout(root);
    assert(std::filesystem::is_directory(layout.recordings));
    assert(std::filesystem::is_directory(layout.labels));
    assert(std::filesystem::is_directory(layout.predictions));
    assert(std::filesystem::exists(layout.readme));

    std::filesystem::remove_all(root);
}

void listsClipsWithMatchingLabels()
{
    const auto root = tempEvalRoot();
    std::string error;
    assert(ensureEvalDataLayout(root, error));

    const auto layout = evalDataLayout(root);
    std::ofstream(layout.recordings / "alpha.wav").put(' ');
    std::ofstream(layout.labels / "alpha.json") << "[]\n";
    std::ofstream(layout.recordings / "orphan.wav").put(' ');
    std::ofstream(layout.labels / "beta.json") << "[]\n";

    const auto clips = listEvalClipNames(root);
    assert(clips.size() == 1);
    assert(clips.front() == "alpha");

    std::filesystem::remove_all(root);
}

} // namespace

int main()
{
    createsEvalDataLayout();
    listsClipsWithMatchingLabels();
    std::cout << "EvalDataPaths tests passed\n";
    return 0;
}
