#include <Voice2VocalSynth/LibriSpeechDataset.h>
#include <Voice2VocalSynth/MfaLabelPipeline.h>
#include <Voice2VocalSynth/PhonemeEvaluation.h>

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
    return std::filesystem::temp_directory_path() / ("voice2vocalsynth-mfa-" + std::to_string(stamp));
}

void stripsStressDigits()
{
    assert(stripMfaArpabetStress("AE0") == "AE");
    assert(stripMfaArpabetStress("ah1") == "AH");
    assert(stripMfaArpabetStress("K") == "K");
    assert(isSkippableMfaPhoneLabel("sil"));
    assert(!isSkippableMfaPhoneLabel("AE0"));
}

void parsesSampleTextGrid()
{
    const auto fixture = repositoryRoot() / "tests/fixtures/mfa/sample_phones.TextGrid";
    std::ifstream input(fixture, std::ios::binary);
    assert(input);
    std::ostringstream contents;
    contents << input.rdbuf();

    const auto parsed = parseMfaPhonesTextGrid(contents.str());
    assert(parsed.ok);
    assert(parsed.frames.size() == 3);
    assert(parsed.frames[0].arpabet == "K");
    assert(parsed.frames[1].arpabet == "AE");
    assert(parsed.frames[2].arpabet == "T");

    const auto reload = parsePhonemeFrameLabelsJson(phonemeFramesToLabelJson(parsed.frames));
    assert(reload.ok);
    assert(reload.frames.size() == 2);
}

void convertsTextGridTree()
{
    const auto root = tempRoot();
    const auto textGridRoot = root / "textgrids";
    const auto labelsRoot = root / "labels";
    std::filesystem::create_directories(textGridRoot);
    std::filesystem::copy_file(repositoryRoot() / "tests/fixtures/mfa/sample_phones.TextGrid",
                               textGridRoot / "1089-134686-0000.TextGrid");

    MfaTextGridConversionSummary summary;
    std::string error;
    assert(convertMfaTextGridTree(textGridRoot, labelsRoot, summary, error));
    assert(error.empty());
    assert(summary.labelFileCount == 1);
    assert(std::filesystem::exists(labelsRoot / "1089-134686-0000.json"));

    std::filesystem::remove_all(root);
}

void listsUtterances()
{
    const auto root = tempRoot();
    const auto datasetRoot = defaultLibriSpeechTestCleanRoot(root);
    const auto chapter = datasetRoot / "1089" / "134686";
    std::filesystem::create_directories(chapter);
    std::ofstream(chapter / "1089-134686-0000.flac", std::ios::binary).put('\0');
    std::ofstream(chapter / "1089-134686-0001.flac", std::ios::binary).put('\0');
    std::ofstream transcript(chapter / "1089-134686.trans.txt");
    transcript << "1089-134686-0001 SECOND\n"
               << "1089-134686-0000 FIRST\n";

    const auto utterances = listLibriSpeechUtterances(datasetRoot, 1);
    assert(utterances.size() == 1);
    assert(utterances.front().id == "1089-134686-0000");

    std::filesystem::remove_all(root);
}

void writesLabelManifest()
{
    const auto root = tempRoot();
    MfaLabelManifestInfo info;
    info.mfaVersion = "3.0.0";
    info.subsetMode = "subset";
    info.utteranceCount = 20;
    info.labelFileCount = 20;
    info.labelsDirectory = defaultLibriSpeechLabelsRoot(root);
    info.datasetRoot = defaultLibriSpeechTestCleanRoot(root);

    const auto manifestPath = defaultLibriSpeechLabelManifestPath(root);
    std::string error;
    assert(writeMfaLabelManifest(info, manifestPath, error));
    assert(error.empty());

    std::ifstream manifest(manifestPath);
    const std::string text((std::istreambuf_iterator<char>(manifest)), std::istreambuf_iterator<char>());
    assert(text.find("montreal-forced-aligner") != std::string::npos);
    assert(text.find("stressDigitsStripped") != std::string::npos);

    std::filesystem::remove_all(root);
}

} // namespace

int main()
{
    stripsStressDigits();
    parsesSampleTextGrid();
    convertsTextGridTree();
    listsUtterances();
    writesLabelManifest();
    std::cout << "MfaLabelPipeline tests passed\n";
    return 0;
}
