#include "MainComponent.h"

#include <memory>
#include <sstream>

namespace
{

[[nodiscard]] juce::File appDataRootDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Voice2VocalSynth");
}

[[nodiscard]] juce::File audioDeviceSettingsFile()
{
    return appDataRootDirectory().getChildFile("audio_device.xml");
}

[[nodiscard]] juce::File shellSettingsFile()
{
    return appDataRootDirectory().getChildFile("shell_settings.json");
}

[[nodiscard]] int loadLatencyPresetIndexFromDisk()
{
    const auto f = shellSettingsFile();
    if (!f.existsAsFile()) {
        return -1;
    }

    const juce::var parsed = juce::JSON::parse(f.loadFileAsString());
    if (parsed.isVoid() || !parsed.hasProperty("latencyPreset")) {
        return -1;
    }

    const int v = static_cast<int>(parsed.getProperty("latencyPreset", 0));
    return juce::jlimit(0, static_cast<int>(Voice2VocalSynth::LatencyPreset::Custom), v);
}

Voice2VocalSynth::AudioDeviceLatency deviceLatencyFromJuce(juce::AudioIODevice* device)
{
    Voice2VocalSynth::AudioDeviceLatency lat;
    if (device == nullptr) {
        return lat;
    }

    lat.sampleRateHz = device->getCurrentSampleRate();
    const int buffer = device->getCurrentBufferSizeSamples();
    lat.inputBufferSizeSamples = buffer;
    lat.outputBufferSizeSamples = buffer;
    lat.inputDeviceLatencySamples = device->getInputLatencyInSamples();
    lat.outputDeviceLatencySamples = device->getOutputLatencyInSamples();
    return lat;
}

std::string breakdownText(const Voice2VocalSynth::LatencyBreakdown& bd)
{
    std::ostringstream out;
    out.setf(std::ios::fixed, std::ios::floatfield);
    out.precision(2);
    for (const auto& c : bd.components()) {
        out << c.name << ": " << c.milliseconds << " ms\n";
    }
    out << "---\n";
    out << "End-to-end (monitoring estimate): " << bd.endToEndMonitoringLatencyMs() << " ms\n";
    return out.str();
}

} // namespace

MainComponent::MainComponent()
{
    titleLabel_.setText("Voice2VocalSynth — JUCE shell", juce::dontSendNotification);
    titleLabel_.setFont(juce::Font(juce::FontOptions(20.0f, juce::Font::bold)));
    titleLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel_);

    hintLabel_.setText(
        "Live pass-through for I/O testing. Settings folder: %LOCALAPPDATA%\\Voice2VocalSynth\\ "
        "(audio_device.xml, shell_settings.json).",
        juce::dontSendNotification);
    hintLabel_.setFont(juce::Font(juce::FontOptions(14.0f)));
    hintLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(hintLabel_);

    addAndMakeVisible(audioSettingsButton_);
    audioSettingsButton_.onClick = [this] { showAudioSettings(); };

    latencyPresetLabel_.setText("Latency preset (analysis budget)", juce::dontSendNotification);
    latencyPresetLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(latencyPresetLabel_);
    addAndMakeVisible(latencyPresetCombo_);

    for (int i = 0; i <= static_cast<int>(Voice2VocalSynth::LatencyPreset::Custom); ++i) {
        const auto preset = static_cast<Voice2VocalSynth::LatencyPreset>(i);
        latencyPresetCombo_.addItem(Voice2VocalSynth::LatencyBudgetCalculator::presetName(preset), i + 1);
    }

    const int loadedPreset = loadLatencyPresetIndexFromDisk();
    const int latencyComboId =
        (loadedPreset >= 0)
            ? (loadedPreset + 1)
            : (static_cast<int>(Voice2VocalSynth::LatencyPreset::Balanced) + 1);
    latencyPresetCombo_.setSelectedId(latencyComboId, juce::dontSendNotification);
    analysisSettings_ = Voice2VocalSynth::LatencyBudgetCalculator::presetSettings(
        static_cast<Voice2VocalSynth::LatencyPreset>(latencyComboId - 1));

    latencyPresetCombo_.onChange = [this] {
        const int id = latencyPresetCombo_.getSelectedId();
        if (id <= 0) {
            return;
        }
        analysisSettings_ = Voice2VocalSynth::LatencyBudgetCalculator::presetSettings(
            static_cast<Voice2VocalSynth::LatencyPreset>(id - 1));
        refreshLatencyDisplay();
        saveShellSettings();
    };

    e2eLabel_.setText("Estimated monitoring latency", juce::dontSendNotification);
    e2eLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(e2eLabel_);

    e2eValueLabel_.setJustificationType(juce::Justification::centredLeft);
    e2eValueLabel_.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    addAndMakeVisible(e2eValueLabel_);

    breakdownLabel_.setText("Breakdown", juce::dontSendNotification);
    addAndMakeVisible(breakdownLabel_);

    breakdownEditor_.setMultiLine(true);
    breakdownEditor_.setReadOnly(true);
    breakdownEditor_.setScrollbarsShown(true);
    breakdownEditor_.setCaretVisible(false);
    breakdownEditor_.setPopupMenuEnabled(true);
    breakdownEditor_.setFont(juce::Font(
        juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
    addAndMakeVisible(breakdownEditor_);

    std::unique_ptr<juce::XmlElement> savedAudioState;
    {
        const auto f = audioDeviceSettingsFile();
        if (f.existsAsFile()) {
            savedAudioState = juce::XmlDocument::parse(f);
        }
    }

    deviceManager.initialise(2, 2, savedAudioState.get(), true);
    deviceManager.addChangeListener(this);
    deviceManager.addAudioCallback(this);

    setSize(640, 520);
    startTimerHz(4);
    refreshLatencyDisplay();
}

MainComponent::~MainComponent()
{
    stopTimer();
    deviceManager.removeChangeListener(this);
    deviceManager.removeAudioCallback(this);
    saveShellSettings();
    saveAudioDeviceSettings();
    deviceManager.closeAudioDevice();
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto r = getLocalBounds().reduced(12);
    titleLabel_.setBounds(r.removeFromTop(28));
    r.removeFromTop(6);
    hintLabel_.setBounds(r.removeFromTop(40));
    r.removeFromTop(10);

    auto buttonRow = r.removeFromTop(28);
    audioSettingsButton_.setBounds(buttonRow.removeFromLeft(180));
    r.removeFromTop(8);
    latencyPresetLabel_.setBounds(r.removeFromTop(20));
    auto presetRow = r.removeFromTop(28);
    latencyPresetCombo_.setBounds(presetRow.removeFromLeft(320));
    r.removeFromTop(14);

    e2eLabel_.setBounds(r.removeFromTop(20));
    e2eValueLabel_.setBounds(r.removeFromTop(26));
    r.removeFromTop(8);
    breakdownLabel_.setBounds(r.removeFromTop(20));
    breakdownEditor_.setBounds(r);
}

void MainComponent::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                     int numInputChannels,
                                                     float* const* outputChannelData,
                                                     int numOutputChannels,
                                                     int numSamples,
                                                     const juce::AudioIODeviceCallbackContext& context)
{
    juce::ignoreUnused(context);

    if (outputChannelData == nullptr || numOutputChannels <= 0 || numSamples <= 0) {
        return;
    }

    const int common = juce::jmin(numInputChannels, numOutputChannels);
    if (inputChannelData != nullptr) {
        for (int ch = 0; ch < common; ++ch) {
            if (inputChannelData[ch] != nullptr && outputChannelData[ch] != nullptr) {
                juce::FloatVectorOperations::copy(outputChannelData[ch], inputChannelData[ch], numSamples);
            }
        }
    }

    for (int ch = common; ch < numOutputChannels; ++ch) {
        if (outputChannelData[ch] != nullptr) {
            juce::FloatVectorOperations::clear(outputChannelData[ch], numSamples);
        }
    }
}

void MainComponent::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    juce::ignoreUnused(device);
}

void MainComponent::audioDeviceStopped()
{
}

void MainComponent::timerCallback()
{
    refreshLatencyDisplay();
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &deviceManager) {
        saveAudioDeviceSettings();
    }
}

void MainComponent::saveAudioDeviceSettings() const
{
    if (auto state = deviceManager.createStateXml()) {
        const auto f = audioDeviceSettingsFile();
        (void)f.getParentDirectory().createDirectory();
        if (!state->writeTo(f)) {
            juce::Logger::writeToLog("Voice2VocalSynth: failed to write " + f.getFullPathName());
        }
    }
}

void MainComponent::saveShellSettings()
{
    juce::DynamicObject::Ptr obj {new juce::DynamicObject()};
    obj->setProperty("latencyPreset", latencyPresetCombo_.getSelectedId() - 1);

    const auto f = shellSettingsFile();
    (void)f.getParentDirectory().createDirectory();
    const juce::String text = juce::JSON::toString(juce::var(obj.get()), false);
    if (!f.replaceWithText(text)) {
        juce::Logger::writeToLog("Voice2VocalSynth: failed to write " + f.getFullPathName());
    }
}

void MainComponent::showAudioSettings()
{
    auto content = std::make_unique<juce::AudioDeviceSelectorComponent>(deviceManager,
                                                                        0,
                                                                        2,
                                                                        0,
                                                                        2,
                                                                        true,
                                                                        true,
                                                                        true,
                                                                        false);
    content->setSize(520, 650);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned(content.release());
    o.dialogTitle = "Audio settings";
    o.dialogBackgroundColour = juce::Colours::lightgrey;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();

    refreshLatencyDisplay();
}

void MainComponent::refreshLatencyDisplay()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr) {
        e2eValueLabel_.setText("No audio device", juce::dontSendNotification);
        breakdownEditor_.clear();
        return;
    }

    const auto ad = deviceLatencyFromJuce(device);
    if (ad.sampleRateHz <= 0.0) {
        e2eValueLabel_.setText("Invalid sample rate", juce::dontSendNotification);
        breakdownEditor_.clear();
        return;
    }

    try {
        const auto bd = Voice2VocalSynth::LatencyBudgetCalculator::calculate(ad, analysisSettings_);
        e2eValueLabel_.setText(juce::String(bd.endToEndMonitoringLatencyMs(), 2) + " ms", juce::dontSendNotification);
        breakdownEditor_.setText(breakdownText(bd));
    } catch (...) {
        e2eValueLabel_.setText("(latency calculation error)", juce::dontSendNotification);
        breakdownEditor_.clear();
    }
}
