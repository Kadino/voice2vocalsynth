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
           ("voice2vocalsynth-linux-audio-" + std::to_string(stamp));
}

std::string readFixture(std::string_view relativePath)
{
    std::ifstream input(repositoryRoot() / relativePath, std::ios::binary);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

void detectsPipeWireLoopbackRoute()
{
    const auto route = detectPipeWireLoopbackRoute(readFixture("tests/fixtures/linux-audio/pactl_info.txt"),
                                                   readFixture("tests/fixtures/linux-audio/pactl_sinks.txt"),
                                                   readFixture("tests/fixtures/linux-audio/pactl_sources.txt"));
    assert(route);
    assert(route->routeId == "pipewire-loopback");
    assert(route->playbackDevice == "LivePhonemeVerify");
    assert(route->captureDevice == "LivePhonemeVerify.monitor");
}

void parsesPactlDeviceNames()
{
    const auto sinks = parsePactlDeviceNames(readFixture("tests/fixtures/linux-audio/pactl_sinks.txt"),
                                             "Sink #");
    assert(sinks.size() == 2);
    assert(sinks.front() == "LivePhonemeVerify");
}

void detectsAlsaLoopbackRoute()
{
    const std::string aplay =
        "**** List of PLAYBACK Hardware Devices ****\n"
        "card 0: Loopback [Loopback], device 0: Loopback PCM [Loopback PCM]\n";
    const std::string arecord =
        "**** List of CAPTURE Hardware Devices ****\n"
        "card 0: Loopback [Loopback], device 1: Loopback PCM [Loopback PCM]\n";
    const auto route = detectAlsaLoopbackRoute(aplay, arecord);
    assert(route);
    assert(route->routeId == "alsa-snd-aloop");
    assert(route->playbackDevice == "plughw:0,0,0");
    assert(route->captureDevice == "plughw:0,1,0");
}

void rejectsMissingRecommendedSink()
{
    const auto route = detectPipeWireLoopbackRoute(readFixture("tests/fixtures/linux-audio/pactl_info.txt"),
                                                   "Sink #0\n",
                                                   readFixture("tests/fixtures/linux-audio/pactl_sources.txt"));
    assert(!route);
}

void parsesInvalidCheckReport()
{
    const auto parsed = parseLinuxVirtualAudioCheckReport(
        "{\"valid\": false, \"error\": \"fixture failure\"}");
    assert(parsed.ok);
    assert(!parsed.validation.valid);
    assert(parsed.validation.error == "fixture failure");
}

void loadsManifestFileRoundTrip()
{
    const auto fixturePath = repositoryRoot() / "tests/fixtures/linux-audio/virtual_audio_manifest.json";
    const auto loaded = loadLinuxVirtualAudioManifestFile(fixturePath);
    assert(loaded.ok);
    assert(loaded.info.route.routeId == "pipewire-loopback");
    assert(loaded.info.route.captureDevice == "LivePhonemeVerify.monitor");
    assert(loaded.info.route.playbackDevice == "LivePhonemeVerify");
}

void monitorSourceNameFollowsSink()
{
    assert(pactlMonitorSourceName("LivePhonemeVerify") == "LivePhonemeVerify.monitor");
}

void parsesCheckReportAndWritesManifest()
{
    const auto parsed = parseLinuxVirtualAudioCheckReport(
        readFixture("tests/fixtures/linux-audio/check_report_ok.json"));
    assert(parsed.ok);
    assert(parsed.validation.valid);
    assert(parsed.validation.route.routeId == "pipewire-loopback");

    const auto root = tempRoot();
    LinuxVirtualAudioManifestInfo info;
    info.route = parsed.validation.route;
    info.probePassed = parsed.validation.probePassed;
    std::string error;
    const auto manifestPath = defaultLinuxVirtualAudioManifestPath(root);
    assert(writeLinuxVirtualAudioManifest(info, manifestPath, error));
    assert(error.empty());

    std::ifstream manifest(manifestPath);
    const std::string text((std::istreambuf_iterator<char>(manifest)), std::istreambuf_iterator<char>());
    assert(text.find("pipewire-loopback") != std::string::npos);
    assert(text.find("LivePhonemeVerify.monitor") != std::string::npos);

    std::filesystem::remove_all(root);
}

} // namespace

int main()
{
    detectsPipeWireLoopbackRoute();
    parsesPactlDeviceNames();
    detectsAlsaLoopbackRoute();
    rejectsMissingRecommendedSink();
    parsesInvalidCheckReport();
    loadsManifestFileRoundTrip();
    monitorSourceNameFollowsSink();
    parsesCheckReportAndWritesManifest();
    std::cout << "LinuxVirtualAudio tests passed\n";
    return 0;
}
