#include "Voice2VocalSynth/PhonemeEvalCli.h"

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
    const auto options = Voice2VocalSynth::parsePhonemeEvalCliArgs(args, error);
    if (!options) {
        std::cerr << error << '\n'
                  << Voice2VocalSynth::phonemeEvalCliUsage() << '\n';
        return static_cast<int>(Voice2VocalSynth::PhonemeEvalCliExitCode::UsageError);
    }

    const auto result = Voice2VocalSynth::runPhonemeEvalCli(*options);
    if (result.exitCode == Voice2VocalSynth::PhonemeEvalCliExitCode::Success) {
        std::cout << result.summary << '\n';
    } else {
        std::cerr << result.summary << '\n';
    }

    return static_cast<int>(result.exitCode);
}
