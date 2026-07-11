#include "Voice2VocalSynth/LinuxVirtualAudioCli.h"

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
    const auto options = Voice2VocalSynth::parseLinuxVirtualAudioCliArgs(args, error);
    if (!options) {
        std::cerr << error << '\n' << Voice2VocalSynth::linuxVirtualAudioCliUsage() << '\n';
        return static_cast<int>(Voice2VocalSynth::LinuxVirtualAudioCliExitCode::UsageError);
    }

    const auto result = Voice2VocalSynth::runLinuxVirtualAudioCli(*options);
    if (result.exitCode == Voice2VocalSynth::LinuxVirtualAudioCliExitCode::Success) {
        std::cout << result.message << '\n';
    } else {
        std::cerr << result.message << '\n';
    }
    return static_cast<int>(result.exitCode);
}
