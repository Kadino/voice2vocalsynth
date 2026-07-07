#include <Voice2VocalSynth/LivePhonemeVerifyPaths.h>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
using namespace Voice2VocalSynth;

std::filesystem::path tempVerifyRoot()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("voice2vocalsynth-live-verify-" + std::to_string(stamp));
}

void createsVerifyLayout()
{
    const auto root = tempVerifyRoot();
    std::string error;
    assert(ensureLivePhonemeVerifyLayout(root, error));
    assert(error.empty());

    const auto layout = livePhonemeVerifyLayout(root);
    assert(std::filesystem::is_directory(layout.datasets));
    assert(std::filesystem::is_directory(layout.labels));
    assert(std::filesystem::is_directory(layout.runs));

    std::filesystem::remove_all(root);
}

void createsTimestampedRun()
{
    const auto root = tempVerifyRoot();
    LivePhonemeVerifyRunPaths paths;
    std::string error;
    assert(createLivePhonemeVerifyRun(root, paths, error));
    assert(error.empty());
    assert(std::filesystem::is_directory(paths.runDirectory));
    assert(paths.liveLog.filename() == "live-log.jsonl");
    assert(paths.manifest.filename() == "manifest.json");

    assert(writeLivePhonemeVerifyManifest(paths, error));
    assert(error.empty());
    assert(std::filesystem::exists(paths.manifest));

    std::ifstream manifest(paths.manifest);
    const std::string text((std::istreambuf_iterator<char>(manifest)), std::istreambuf_iterator<char>());
    assert(text.find("montreal-forced-aligner") != std::string::npos);
    assert(text.find("pipewire-loopback") != std::string::npos);

    std::filesystem::remove_all(root);
}

} // namespace

int main()
{
    createsVerifyLayout();
    createsTimestampedRun();
    std::cout << "LivePhonemeVerifyPaths tests passed\n";
    return 0;
}
