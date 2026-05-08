#include "MainComponent.h"

#include <sstream>

namespace
{

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

    hintLabel_.setText("Live pass-through for I/O testing. Use Audio settings to pick devices and buffer size.",
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
    latencyPresetCombo_.setSelectedId(static_cast<int>(Voice2VocalSynth::LatencyPreset::Balanced) + 1,
                                      juce::dontSendNotification);
    latencyPresetCombo_.onChange = [this] {
        const int id = latencyPresetCombo_.getSelectedId();
        if (id <= 0) {
            return;
        }
        analysisSettings_ = Voice2VocalSynth::LatencyBudgetCalculator::presetSettings(
            static_cast<Voice2VocalSynth::LatencyPreset>(id - 1));
        refreshLatencyDisplay();
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

    deviceManager.initialise(2, 2, nullptr, true);
    deviceManager.addAudioCallback(this);

    setSize(640, 520);
    startTimerHz(4);
    refreshLatencyDisplay();
}

MainComponent::~MainComponent()
{
    stopTimer();
    deviceManager.removeAudioCallback(this);
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
