#include <Voice2VocalSynth/LibriSpeechPlaybackCli.h>
#include <Voice2VocalSynth/LibriSpeechDataset.h>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace
{
using namespace Voice2VocalSynth;

std::filesystem::path repositoryRoot()
{
#ifdef VOICE2VOCALSYNTH_REPOSITORY_ROOT
    return std::filesystem::path{VOICE2VOCALSYNTH_REPOSITORY_ROOT};
#else
    return std::filesystem::current_path();
#endif
}

std::filesystem::path tempRoot()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("voice2vocalsynth-playback-cli-" + std::to_string(stamp));
}

void writeTextFile(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << text;
}

void seedMiniDataset(const std::filesystem::path& datasetRoot)
{
    const auto chapter = datasetRoot / "1089" / "134686";
    std::filesystem::create_directories(chapter);
    std::ofstream(chapter / "1089-134686-0000.flac", std::ios::binary).put('\0');
    writeTextFile(chapter / "1089-134686.trans.txt",
                  "1089-134686-0000 FIRST UTTERANCE\n");
}

void seedVerifyRoot(const std::filesystem::path& verifyRoot)
{
    const auto manifestSource = repositoryRoot() / "tests/fixtures/linux-audio/virtual_audio_manifest.json";
    const auto manifestDest = defaultLinuxVirtualAudioManifestPath(verifyRoot);
    std::filesystem::create_directories(manifestDest.parent_path());
    std::filesystem::copy_file(manifestSource, manifestDest);
    seedMiniDataset(defaultLibriSpeechTestCleanRoot(verifyRoot));
}

void parsesBuildManifestArgs()
{
    std::string error;
    const auto options = parseLibriSpeechPlaybackCliArgs(
        {"Voice2VocalSynthLibriSpeechPlayback",
         "--build-manifest",
         "--durations-tsv",
         "/tmp/durations.tsv",
         "--verify-root",
         "/tmp/verify",
         "--run-dir",
         "/tmp/run",
         "--manifest",
         "/tmp/playback-manifest.json",
         "--utterance-id",
         "1089-134686-0000",
         "--gap-seconds",
         "0.25",
         "--playback-device",
         "CustomSink"},
        error);
    assert(options);
    assert(error.empty());
    assert(options->action == LibriSpeechPlaybackCliAction::BuildManifest);
    assert(options->durationsTsv == "/tmp/durations.tsv");
    assert(options->verifyRoot == "/tmp/verify");
    assert(*options->runDirectoryOverride == "/tmp/run");
    assert(*options->manifestPathOverride == "/tmp/playback-manifest.json");
    assert(*options->utteranceId == "1089-134686-0000");
    assert(options->gapSeconds == 0.25);
    assert(*options->playbackDeviceOverride == "CustomSink");
}

void rejectsMissingDurationsTsv()
{
    std::string error;
    assert(!parseLibriSpeechPlaybackCliArgs(
        {"Voice2VocalSynthLibriSpeechPlayback", "--build-manifest"}, error));
    assert(!error.empty());
}

void buildsManifestFromFixtureDataset()
{
    const auto root = tempRoot();
    seedVerifyRoot(root);

    const auto runDir = root / "runs" / "cli-test";
    const auto manifestPath = runDir / "playback-manifest.json";
    const auto durationsTsv = repositoryRoot() / "tests/fixtures/linux-audio/utterance_durations.tsv";

    LibriSpeechPlaybackCliOptions options;
    options.verifyRoot = root;
    options.durationsTsv = durationsTsv;
    options.runDirectoryOverride = runDir;
    options.manifestPathOverride = manifestPath;
    options.utteranceId = "1089-134686-0000";
    options.gapSeconds = 0.5;

    const auto result = runLibriSpeechPlaybackCli(options);
    assert(result.exitCode == LibriSpeechPlaybackCliExitCode::Success);
    assert(result.message.find("clips: 1") != std::string::npos);
    assert(std::filesystem::exists(manifestPath));

    std::ifstream manifest(manifestPath);
    const std::string text((std::istreambuf_iterator<char>(manifest)), std::istreambuf_iterator<char>());
    assert(text.find("1089-134686-0000") != std::string::npos);
    assert(text.find("LivePhonemeVerify") != std::string::npos);
    assert(text.find("\"realtime\": true") != std::string::npos);

    std::filesystem::remove_all(root);
}

} // namespace

int main()
{
    parsesBuildManifestArgs();
    rejectsMissingDurationsTsv();
    buildsManifestFromFixtureDataset();
    std::cout << "LibriSpeechPlaybackCli tests passed\n";
    return 0;
}
