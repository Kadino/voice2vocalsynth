#include <Voice2VocalSynth/PhonemeBakeoffCli.h>

#include <cassert>
#include <iostream>
#include <string>

namespace
{
using namespace Voice2VocalSynth;

void parsesEvalDataClipArguments()
{
    std::string error;
    const auto options = parsePhonemeBakeoffCliArgs(
        {"Voice2VocalSynthPhonemeBakeoff",
         "--eval-data",
         "/tmp/EvalData",
         "--clip",
         "spoken_vowels",
         "--backends",
         "placeholder"},
        error);
    assert(options);
    assert(options->evalDataRoot == "/tmp/EvalData");
    assert(options->clipName == "spoken_vowels");
    assert(options->referencePath == "/tmp/EvalData/labels/spoken_vowels.json");
    assert(options->audioPath == "/tmp/EvalData/recordings/spoken_vowels.wav");
}

void parsesAllClipsArguments()
{
    std::string error;
    const auto options = parsePhonemeBakeoffCliArgs(
        {"Voice2VocalSynthPhonemeBakeoff",
         "--eval-data",
         "/tmp/EvalData",
         "--all-clips",
         "--backends",
         "placeholder"},
        error);
    assert(options);
    assert(options->allClips);
    assert(options->evalDataRoot == "/tmp/EvalData");
    assert(!options->clipName);
}

void rejectsAllClipsWithoutEvalData()
{
    std::string error;
    const auto options = parsePhonemeBakeoffCliArgs(
        {"Voice2VocalSynthPhonemeBakeoff", "--all-clips"},
        error);
    assert(!options);
    assert(!error.empty());
}

void parsesPocketsphinxModelRoot()
{
    std::string error;
    const auto options = parsePhonemeBakeoffCliArgs(
        {"Voice2VocalSynthPhonemeBakeoff",
         "--reference",
         "/tmp/ref.json",
         "--audio",
         "/tmp/audio.wav",
         "--backends",
         "pocketsphinx",
         "--pocketsphinx-model-root",
         "/tmp/model"},
        error);
    assert(options);
    assert(options->pocketSphinxModelRoot == "/tmp/model");
    assert(options->backendNames.size() == 1);
    assert(options->backendNames[0] == "pocketsphinx");
}

} // namespace

int main()
{
    parsesEvalDataClipArguments();
    parsesAllClipsArguments();
    rejectsAllClipsWithoutEvalData();
    parsesPocketsphinxModelRoot();
    std::cout << "PhonemeBakeoffCli tests passed\n";
    return 0;
}
