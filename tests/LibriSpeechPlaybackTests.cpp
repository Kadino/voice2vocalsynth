#include <Voice2VocalSynth/LibriSpeechPlayback.h>
#include <Voice2VocalSynth/LinuxVirtualAudio.h>

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
           ("voice2vocalsynth-playback-" + std::to_string(stamp));
}

std::string readFixture(std::string_view relativePath)
{
    std::ifstream input(repositoryRoot() / relativePath, std::ios::binary);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

void parsesDurationTsv()
{
    std::string error;
    const auto durations =
        parseLibriSpeechDurationTsv(readFixture("tests/fixtures/linux-audio/utterance_durations.tsv"),
                                    error);
    assert(error.empty());
    assert(durations.size() == 2);
    assert(durations[0].utteranceId == "1089-134686-0000");
    assert(std::abs(durations[0].durationSeconds - 3.25) < 1.0e-6);
}

void buildsPlaybackPlanWithGaps()
{
    std::vector<LibriSpeechUtterance> utterances(2);
    utterances[0].id = "1089-134686-0000";
    utterances[0].flacPath = "/tmp/a.flac";
    utterances[1].id = "1089-134686-0001";
    utterances[1].flacPath = "/tmp/b.flac";

    std::string error;
    const auto durations =
        parseLibriSpeechDurationTsv(readFixture("tests/fixtures/linux-audio/utterance_durations.tsv"),
                                    error);
    assert(error.empty());

    LinuxVirtualAudioRoute route;
    route.routeId = "pipewire-loopback";
    route.playbackDevice = "LivePhonemeVerify";
    route.captureDevice = "LivePhonemeVerify.monitor";

    const auto built = buildLibriSpeechPlaybackPlan(utterances,
                                                    durations,
                                                    route,
                                                    kDefaultLibriSpeechPlaybackGapSeconds,
                                                    "/tmp/run");
    assert(built.ok);
    assert(built.plan.clips.size() == 2);
    assert(std::abs(built.plan.clips[0].startOffsetSeconds - 0.0) < 1.0e-6);
    assert(std::abs(built.plan.clips[1].startOffsetSeconds - 3.75) < 1.0e-6);
    assert(std::abs(built.plan.totalDurationSeconds - 6.25) < 1.0e-6);
}

void loadsVirtualAudioManifest()
{
    const auto loaded = loadLinuxVirtualAudioManifest(
        readFixture("tests/fixtures/linux-audio/virtual_audio_manifest.json"));
    assert(loaded.ok);
    assert(loaded.info.route.playbackDevice == "LivePhonemeVerify");
    assert(loaded.info.route.routeId == "pipewire-loopback");
}

void writesPlaybackManifest()
{
    LibriSpeechPlaybackPlan plan;
    plan.gapSeconds = 0.5;
    plan.totalDurationSeconds = 3.25;
    plan.playbackDevice = "LivePhonemeVerify";
    plan.routeId = "pipewire-loopback";
    plan.runDirectory = "/tmp/run";
    plan.playbackStartedSteadyNs = 123456789;
    plan.clips.push_back({"1089-134686-0000", "/tmp/a.flac", 3.25, 0.0});

    const auto root = tempRoot();
    const auto manifestPath = defaultLibriSpeechPlaybackManifestPath(root);
    std::string error;
    assert(writeLibriSpeechPlaybackManifest(plan, manifestPath, error));
    assert(error.empty());

    std::ifstream manifest(manifestPath);
    const std::string text((std::istreambuf_iterator<char>(manifest)), std::istreambuf_iterator<char>());
    assert(text.find("\"realtime\": true") != std::string::npos);
    assert(text.find("startOffsetSeconds") != std::string::npos);
    const auto loaded = parseLibriSpeechPlaybackManifest(text);
    assert(loaded.ok);
    assert(loaded.plan.playbackStartedSteadyNs == 123456789);
    assert(loaded.plan.clips.size() == 1);
    assert(loaded.plan.clips[0].utteranceId == "1089-134686-0000");

    std::filesystem::remove_all(root);
}

} // namespace

int main()
{
    parsesDurationTsv();
    buildsPlaybackPlanWithGaps();
    loadsVirtualAudioManifest();
    writesPlaybackManifest();
    std::cout << "LibriSpeechPlayback tests passed\n";
    return 0;
}
