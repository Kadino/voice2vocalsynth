#include <Voice2VocalSynth/LibriSpeechDataset.h>
#include <Voice2VocalSynth/LibriSpeechDatasetCli.h>

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
           ("voice2vocalsynth-librispeech-" + std::to_string(stamp));
}

void createMiniDataset(const std::filesystem::path& root)
{
    const auto chapter = root / "1089" / "134686";
    std::filesystem::create_directories(chapter);
    std::ofstream(chapter / "1089-134686-0000.flac", std::ios::binary).put('\0');
    std::ofstream transcript(chapter / "1089-134686.trans.txt");
    transcript << "1089-134686-0000 A TEST TRANSCRIPT\n";
}

void validatesMiniDataset()
{
    const auto root = tempVerifyRoot() / "LibriSpeech" / "test-clean";
    createMiniDataset(root);

    const auto validation = validateLibriSpeechTestClean(root);
    assert(validation.valid);
    assert(validation.summary.speakerCount == 1);
    assert(validation.summary.chapterCount == 1);
    assert(validation.summary.flacCount == 1);
    assert(validation.summary.transcriptFileCount == 1);

    std::filesystem::remove_all(root.parent_path().parent_path());
}

void discoversDefaultLayout()
{
    const auto verifyRoot = tempVerifyRoot();
    const auto datasetRoot = defaultLibriSpeechTestCleanRoot(verifyRoot);
    createMiniDataset(datasetRoot);

    std::string note;
    const auto discovered = discoverLibriSpeechTestCleanRoot(verifyRoot, note);
    assert(discovered);
    assert(*discovered == datasetRoot);
    assert(!note.empty());

    std::filesystem::remove_all(verifyRoot);
}

void writesManifest()
{
    const auto verifyRoot = tempVerifyRoot();
    const auto datasetRoot = defaultLibriSpeechTestCleanRoot(verifyRoot);
    createMiniDataset(datasetRoot);

    const auto validation = validateLibriSpeechTestClean(datasetRoot);
    assert(validation.valid);

    const auto manifestPath = defaultLibriSpeechDatasetManifestPath(verifyRoot);
    std::string error;
    assert(writeLibriSpeechDatasetManifest(validation.summary, manifestPath, error));
    assert(error.empty());
    assert(std::filesystem::exists(manifestPath));

    std::ifstream manifest(manifestPath);
    const std::string text((std::istreambuf_iterator<char>(manifest)), std::istreambuf_iterator<char>());
    assert(text.find("librispeech-test-clean") != std::string::npos);
    assert(text.find("montreal-forced-aligner") != std::string::npos);

    std::filesystem::remove_all(verifyRoot);
}

void parsesCliArgs()
{
    std::string error;
    const std::vector<std::string> args {
        "Voice2VocalSynthLibriSpeechSetup",
        "--write-manifest",
        "--verify-root",
        "/tmp/verify",
    };
    const auto options = parseLibriSpeechDatasetCliArgs(args, error);
    assert(options);
    assert(options->action == LibriSpeechDatasetCliAction::WriteManifest);
    assert(options->verifyRoot == "/tmp/verify");
    assert(error.empty());
}

} // namespace

int main()
{
    validatesMiniDataset();
    discoversDefaultLayout();
    writesManifest();
    parsesCliArgs();
    std::cout << "LibriSpeechDataset tests passed\n";
    return 0;
}
