#include <JuceHeader.h>

#include "MainComponent.h"
#include "Voice2VocalSynth/ShellCli.h"

#include <optional>
#include <vector>

class Voice2VocalSynthApplication final : public juce::JUCEApplication
{
public:
    [[nodiscard]] const juce::String getApplicationName() override
    {
        return ProjectInfo::projectName;
    }

    [[nodiscard]] const juce::String getApplicationVersion() override
    {
        return ProjectInfo::versionString;
    }

    [[nodiscard]] bool moreThanOneInstanceAllowed() override
    {
        return false;
    }

    void initialise(const juce::String& commandLineParameters) override
    {
        std::optional<Voice2VocalSynth::ShellLiveLogExportPaths> liveLogExportPaths;
        juce::StringArray tokens;
        tokens.addTokens(commandLineParameters, true);
        std::vector<std::string> args;
        args.push_back("Voice2VocalSynthApp");
        for (const auto& token : tokens) {
            args.push_back(token.toStdString());
        }

        std::string error;
        Voice2VocalSynth::ShellLiveLogExportOptions launchOptions;
        if (const auto options = Voice2VocalSynth::parseShellLiveLogExportArgs(args, error)) {
            launchOptions = *options;
            if (options->enabled) {
                Voice2VocalSynth::ShellLiveLogExportPaths paths;
                if (!Voice2VocalSynth::resolveShellLiveLogExportPaths(*options, paths, error)) {
                    juce::Logger::writeToLog("Voice2VocalSynth: live log export setup failed: "
                                             + juce::String(error));
                    setApplicationReturnValue(1);
                    quit();
                    return;
                } else {
                    liveLogExportPaths = std::move(paths);
                }
            }
        } else if (!error.empty()) {
            juce::Logger::writeToLog("Voice2VocalSynth: " + juce::String(error));
            setApplicationReturnValue(1);
            quit();
            return;
        }

        mainWindow = std::make_unique<MainWindow>(getApplicationName(),
                                                  std::move(launchOptions),
                                                  std::move(liveLogExportPaths));
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(const juce::String& name,
                            Voice2VocalSynth::ShellLiveLogExportOptions launchOptions,
                            std::optional<Voice2VocalSynth::ShellLiveLogExportPaths> liveLogExportPaths)
            : DocumentWindow(name,
                             juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                                 juce::ResizableWindow::backgroundColourId),
                             juce::DocumentWindow::minimiseButton | juce::DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(std::move(launchOptions),
                                              std::move(liveLogExportPaths)),
                            true);
            setResizable(true, true);
            setResizeLimits(420, 380, 10000, 10000);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(Voice2VocalSynthApplication)
