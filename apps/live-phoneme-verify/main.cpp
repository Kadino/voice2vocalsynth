#include <Voice2VocalSynth/LivePhonemeVerifyCli.h>

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    const auto result = Voice2VocalSynth::runLivePhonemeVerifyCli(arguments);
    std::cout << result.message << '\n';
    return static_cast<int>(result.exitCode);
}
