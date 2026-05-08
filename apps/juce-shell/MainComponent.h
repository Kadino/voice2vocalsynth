#pragma once

#include <JuceHeader.h>

#include "Voice2VocalSynth/LatencyBudget.h"

class MainComponent final : public juce::Component,
                            private juce::AudioIODeviceCallback,
                            private juce::Timer,
                            private juce::ChangeListener
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

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    void showAudioSettings();
    void refreshLatencyDisplay();
    void saveAudioDeviceSettings() const;
    void saveShellSettings();
    void loadShellSettingsFromDisk();

    void offlineRenderTest();
    void runOfflineRenderToFile(const juce::File& voicebankDirectory, const juce::File& outputWavFile);

    juce::AudioDeviceManager deviceManager;

    juce::Label titleLabel_;
    juce::Label hintLabel_;
    juce::TextButton audioSettingsButton_ {"Audio settings..."};
    juce::TextButton offlineRenderButton_ {"Offline render test…"};
    juce::Label latencyPresetLabel_;
    juce::ComboBox latencyPresetCombo_;
    juce::Label offlinePhraseLabel_;
    juce::Label offlinePhonemesLabel_;
    juce::TextEditor offlinePhonemesEditor_;
    juce::Label offlineNoteLabel_;
    juce::TextEditor offlineNoteEditor_;
    juce::Label e2eLabel_;
    juce::Label e2eValueLabel_;
    juce::Label breakdownLabel_;
    juce::TextEditor breakdownEditor_;

    Voice2VocalSynth::AnalysisLatencySettings analysisSettings_ =
        Voice2VocalSynth::LatencyBudgetCalculator::presetSettings(Voice2VocalSynth::LatencyPreset::Balanced);
};
