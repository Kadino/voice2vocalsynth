#include <Voice2VocalSynth/LibriSpeechPlayback.h>
#include <Voice2VocalSynth/LiveLogFixture.h>
#include <Voice2VocalSynth/LiveLogFixtureCli.h>
#include <Voice2VocalSynth/LivePhonemeVerification.h>
#include <Voice2VocalSynth/LivePhonemeVerifyCli.h>
#include <Voice2VocalSynth/MfaLabelPipeline.h>
#include <Voice2VocalSynth/ShellCli.h>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

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
           ("voice2vocalsynth-live-log-fixture-" + std::to_string(stamp));
}

LiveVerificationGateOptions passingGates()
{
    LiveVerificationGateOptions gates;
    gates.minF1 = 0.5;
    gates.maxMeanOnsetErrorMs = 50.0;
    gates.maxP95OnsetErrorMs = 50.0;
    gates.maxMeanEndErrorMs = 50.0;
    gates.maxP95EndErrorMs = 50.0;
    gates.maxMeanDurationErrorMs = 50.0;
    gates.maxMissedConsonantRate = 0.5;
    return gates;
}

void mapsBackendCliNames()
{
    assert(liveLogFixtureSessionBackend("pocketsphinx") == "pocketsphinx_allphone");
    assert(liveLogFixtureSessionBackend("onnx_phoneme") == "phoneme_onnx");
    assert(liveLogFixtureSessionBackend("placeholder") == "placeholder_pitch_gate");
}

void generatesPhaseFiveRecords()
{
    LiveLogFixtureOptions options;
    options.runPaths.runDirectory = "/tmp/run";
    options.runPaths.liveLog = "/tmp/run/live-log.jsonl";
    options.runPaths.manifest = "/tmp/run/manifest.json";
    options.backendCli = "pocketsphinx";

    const auto content = generateLiveLogFixture(options);
    assert(content.jsonl.find("\"kind\":\"session_start\"") != std::string::npos);
    assert(content.jsonl.find("\"startup_ok\":true") != std::string::npos);
    assert(content.jsonl.find("\"kind\":\"backend_descriptor\"") != std::string::npos);
    assert(content.jsonl.find("\"kind\":\"device_settings\"") != std::string::npos);
    assert(content.jsonl.find("pocketsphinx_allphone") != std::string::npos);
}

void generatesPhaseSixPhonemeFrames()
{
    LiveLogFixtureOptions options;
    options.runPaths.runDirectory = "/tmp/run";
    options.runPaths.liveLog = "/tmp/run/live-log.jsonl";
    options.runPaths.manifest = "/tmp/run/manifest.json";
    options.backendCli = "pocketsphinx";

    const auto parsed = parseLivePhonemeLogJsonl(generateLiveLogFixture(options).jsonl);
    assert(parsed.ok);
    assert(parsed.log.sessionBackend == "pocketsphinx_allphone");
    assert(!parsed.log.phonemeFrames.empty());
    assert(!parsed.log.backendLatencies.empty());
    assert(parsed.log.phonemeFrames.front().steadyNs > parsed.log.sessionSteadyNs);
    assert(parsed.log.phonemeFrames.back().steadyNs >= parsed.log.phonemeFrames.front().steadyNs);
    assert(parsed.log.phonemeFrames.front().frame.estimatedOnsetSeconds >
           parsed.log.sessionStreamTimeSeconds);
}

void scoresPhaseSevenWithMfaLabels()
{
    const auto root = tempRoot();
    const auto runDir = root / "run";
    const auto labelsRoot = root / "labels";
    std::filesystem::create_directories(labelsRoot);

    const auto textGridRoot = root / "textgrids";
    std::filesystem::create_directories(textGridRoot);
    std::filesystem::copy_file(repositoryRoot() / "tests/fixtures/mfa/sample_phones.TextGrid",
                                 textGridRoot / "1089-134686-0000.TextGrid");
    MfaTextGridConversionSummary summary;
    std::string error;
    assert(convertMfaTextGridTree(textGridRoot, labelsRoot, summary, error));

    LiveLogFixtureOptions fixtureOptions;
    fixtureOptions.runPaths.runDirectory = runDir;
    fixtureOptions.runPaths.liveLog = runDir / "live-log.jsonl";
    fixtureOptions.runPaths.manifest = runDir / "manifest.json";
    fixtureOptions.backendCli = "pocketsphinx";
    assert(writeLiveLogFixture(fixtureOptions, error));

    LibriSpeechPlaybackPlan playback;
    playback.playbackStartedSteadyNs = fixtureOptions.playbackAnchorSteadyNs;
    playback.clips.push_back({"1089-134686-0000",
                              "/tmp/1089-134686-0000.flac",
                              0.55,
                              0.0,
                              fixtureOptions.playbackAnchorSteadyNs});
    playback.totalDurationSeconds = 0.55;

    const auto liveLog = loadLivePhonemeLogJsonl(fixtureOptions.runPaths.liveLog);
    assert(liveLog.ok);
    const auto verified = verifyLivePhonemeRun(liveLog.log,
                                               playback,
                                               labelsRoot,
                                               "pocketsphinx",
                                               passingGates());
    assert(verified.ok);
    assert(verified.report.gates.passed);
    assert(verified.report.quality.f1 == 1.0);

    std::filesystem::remove_all(root);
}

void headlessCliWritesLogAndExits()
{
    const auto root = tempRoot();
    const auto liveLog = root / "live-log.jsonl";
    const auto quitFile = root / "quit-request";

    std::vector<std::string> args {
        "Voice2VocalSynthLiveLogFixture",
        "--live-log-export",
        "--live-log-out",
        liveLog.string(),
        "--phoneme-backend",
        "pocketsphinx",
        "--capture-device",
        "LivePhonemeVerify.monitor",
        "--quit-file",
        quitFile.string(),
    };

    std::thread writer([&quitFile]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::ofstream(quitFile).put('\n');
    });

    const auto result = runLiveLogFixtureCli(args);
    writer.join();

    assert(result.exitCode == LiveLogFixtureCliExitCode::Success);
    assert(std::filesystem::exists(liveLog));
    const auto parsed = loadLivePhonemeLogJsonl(liveLog);
    assert(parsed.ok);
    assert(parsed.log.sessionBackend == "pocketsphinx_allphone");

    std::filesystem::remove_all(root);
}

void shellArgsCompatibleWithJuceExport()
{
    std::string error;
    const auto options = parseShellLiveLogExportArgs(
        {"Voice2VocalSynthLiveLogFixture",
         "--live-log-export",
         "--live-log-out",
         "/tmp/live-log.jsonl",
         "--phoneme-backend",
         "pocketsphinx",
         "--capture-device",
         "LivePhonemeVerify.monitor",
         "--quit-file",
         "/tmp/quit"},
        error);
    assert(options);
    assert(error.empty());
    assert(options->enabled);
    assert(*options->phonemeBackend == "pocketsphinx");
}

} // namespace

int main()
{
    mapsBackendCliNames();
    generatesPhaseFiveRecords();
    generatesPhaseSixPhonemeFrames();
    scoresPhaseSevenWithMfaLabels();
    headlessCliWritesLogAndExits();
    shellArgsCompatibleWithJuceExport();
    std::cout << "LiveLogFixture tests passed\n";
    return 0;
}
