#pragma once

#include <JuceHeader.h>

#include "Voice2VocalSynth/LatencyBudget.h"

class MainComponent final : public juce::Component,
                            private juce::AudioIODeviceCallback,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    void timerCallback() override;

    void showAudioSettings();
    void refreshLatencyDisplay();

    juce::AudioDeviceManager deviceManager;

    juce::Label titleLabel_;
    juce::Label hintLabel_;
    juce::TextButton audioSettingsButton_ {"Audio settings..."};
    juce::Label latencyPresetLabel_;
    juce::ComboBox latencyPresetCombo_;
    juce::Label e2eLabel_;
    juce::Label e2eValueLabel_;
    juce::Label breakdownLabel_;
    juce::TextEditor breakdownEditor_;

    Voice2VocalSynth::AnalysisLatencySettings analysisSettings_ =
        Voice2VocalSynth::LatencyBudgetCalculator::presetSettings(Voice2VocalSynth::LatencyPreset::Balanced);
};
