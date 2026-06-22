#include "Voice2VocalSynth/PhonemeBakeoffCli.h"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        args.emplace_back(argv[index]);
    }

    std::string error;
    const auto options = Voice2VocalSynth::parsePhonemeBakeoffCliArgs(args, error);
    if (!options) {
        std::cerr << error << '\n' << Voice2VocalSynth::phonemeBakeoffCliUsage() << '\n';
        return static_cast<int>(Voice2VocalSynth::PhonemeBakeoffCliExitCode::UsageError);
    }

    const auto result = Voice2VocalSynth::runPhonemeBakeoffCli(*options);
    if (result.exitCode == Voice2VocalSynth::PhonemeBakeoffCliExitCode::Success) {
        std::cout << result.summary;
    } else {
        std::cerr << result.summary << '\n';
    }
    return static_cast<int>(result.exitCode);
}
