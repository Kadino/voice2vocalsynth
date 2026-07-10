#include "Voice2VocalSynth/LibriSpeechPlaybackCli.h"

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
    const auto options = Voice2VocalSynth::parseLibriSpeechPlaybackCliArgs(args, error);
    if (!options) {
        std::cerr << error << '\n' << Voice2VocalSynth::libriSpeechPlaybackCliUsage() << '\n';
        return static_cast<int>(Voice2VocalSynth::LibriSpeechPlaybackCliExitCode::UsageError);
    }

    const auto result = Voice2VocalSynth::runLibriSpeechPlaybackCli(*options);
    if (result.exitCode == Voice2VocalSynth::LibriSpeechPlaybackCliExitCode::Success) {
        std::cout << result.message << '\n';
    } else {
        std::cerr << result.message << '\n';
    }
    return static_cast<int>(result.exitCode);
}
