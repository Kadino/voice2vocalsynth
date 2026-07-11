#include <Voice2VocalSynth/MfaLabelCli.h>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

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
           ("voice2vocalsynth-mfa-cli-" + std::to_string(stamp));
}

void parsesConvertTextGridsArgs()
{
    std::string error;
    const auto options = parseMfaLabelCliArgs(
        {"Voice2VocalSynthMfaLabelConvert",
         "--convert-textgrids",
         "--textgrid-root",
         "/tmp/textgrids",
         "--labels-root",
         "/tmp/labels",
         "--verify-root",
         "/tmp/verify"},
        error);
    assert(options);
    assert(error.empty());
    assert(options->action == MfaLabelCliAction::ConvertTextGrids);
    assert(options->textGridRoot == "/tmp/textgrids");
    assert(options->verifyRoot == "/tmp/verify");
    assert(options->labelsRoot == defaultLibriSpeechLabelsRoot("/tmp/verify"));
}

void parsesWriteManifestArgs()
{
    std::string error;
    const auto options = parseMfaLabelCliArgs(
        {"Voice2VocalSynthMfaLabelConvert",
         "--write-manifest",
         "--mfa-version",
         "3.0.0",
         "--subset-mode",
         "subset",
         "--utterance-count",
         "20",
         "--label-file-count",
         "20",
         "--manifest",
         "/tmp/manifest.json"},
        error);
    assert(options);
    assert(error.empty());
    assert(options->action == MfaLabelCliAction::WriteManifest);
    assert(options->mfaVersion == "3.0.0");
    assert(options->subsetMode == "subset");
    assert(options->utteranceCount == 20);
    assert(options->labelFileCount == 20);
    assert(options->manifestPath == "/tmp/manifest.json");
}

void rejectsMissingAction()
{
    std::string error;
    assert(!parseMfaLabelCliArgs({"Voice2VocalSynthMfaLabelConvert"}, error));
    assert(!error.empty());
}

void convertsFixtureTextGridsViaCli()
{
    const auto root = tempRoot();
    const auto textGridRoot = root / "textgrids";
    const auto labelsRoot = root / "labels";
    std::filesystem::create_directories(textGridRoot);
    std::filesystem::copy_file(repositoryRoot() / "tests/fixtures/mfa/sample_phones.TextGrid",
                               textGridRoot / "1089-134686-0000.TextGrid");

    MfaLabelCliOptions options;
    options.action = MfaLabelCliAction::ConvertTextGrids;
    options.textGridRoot = textGridRoot;
    options.labelsRoot = labelsRoot;

    const auto result = runMfaLabelCli(options);
    assert(result.exitCode == MfaLabelCliExitCode::Success);
    assert(result.message.find("labels: 1") != std::string::npos);
    assert(std::filesystem::exists(labelsRoot / "1089-134686-0000.json"));

    std::filesystem::remove_all(root);
}

void writesManifestViaCli()
{
    const auto root = tempRoot();
    MfaLabelCliOptions options;
    options.action = MfaLabelCliAction::WriteManifest;
    options.verifyRoot = root;
    options.mfaVersion = "3.0.0";
    options.subsetMode = "subset";
    options.utteranceCount = 1;
    options.labelFileCount = 1;
    options.labelsRoot = defaultLibriSpeechLabelsRoot(root);
    options.datasetRoot = defaultLibriSpeechTestCleanRoot(root);
    options.manifestPath = defaultLibriSpeechLabelManifestPath(root);

    const auto result = runMfaLabelCli(options);
    assert(result.exitCode == MfaLabelCliExitCode::Success);
    assert(std::filesystem::exists(options.manifestPath));

    std::ifstream manifest(options.manifestPath);
    const std::string text((std::istreambuf_iterator<char>(manifest)), std::istreambuf_iterator<char>());
    assert(text.find("stressDigitsStripped") != std::string::npos);
    assert(text.find("3.0.0") != std::string::npos);

    std::filesystem::remove_all(root);
}

} // namespace

int main()
{
    parsesConvertTextGridsArgs();
    parsesWriteManifestArgs();
    rejectsMissingAction();
    convertsFixtureTextGridsViaCli();
    writesManifestViaCli();
    std::cout << "MfaLabelCli tests passed\n";
    return 0;
}
