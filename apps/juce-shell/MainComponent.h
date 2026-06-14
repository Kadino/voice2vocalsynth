#pragma once

#include <JuceHeader.h>

#include "Voice2VocalSynth/InferenceLatencyTracker.h"
#include "Voice2VocalSynth/LatencyBudget.h"
#include "Voice2VocalSynth/LoopbackLatencyMeasurer.h"
#include "Voice2VocalSynth/PhonemeTemporalStabilizer.h"
#include "Voice2VocalSynth/PitchHistory.h"
#include "Voice2VocalSynth/PitchTarget.h"
#include "Voice2VocalSynth/PlaybackBoundaryMapper.h"
#include "Voice2VocalSynth/UtteranceSustainReleasePolicy.h"
#include "Voice2VocalSynth/VoiceActivityDetector.h"
#include "Voice2VocalSynth/WhistleDetector.h"

#if defined(VOICE2VOCALSYNTH_WITH_ONNX)
#include "Voice2VocalSynth/PhonemeOnnxAsyncRunner.h"
#endif

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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
    void beginLoopbackMeasurement();
    [[nodiscard]] double measuredEndToEndMsForMapping() const;
    void saveAudioDeviceSettings() const;
    void saveShellSettings();
    void loadShellSettingsFromDisk();

    void livePipelineTimerTick();
    void pushLiveLogLine(const std::string& line);

    void offlineRenderTest();
    void runOfflineRenderToFile(const juce::File& voicebankDirectory, const juce::File& outputWavFile);

    juce::AudioDeviceManager deviceManager;

    juce::Label titleLabel_;
    juce::Label hintLabel_;
    juce::TextButton audioSettingsButton_ {"Audio settings..."};
    juce::TextButton offlineRenderButton_ {"Offline render test…"};
    juce::Label latencyPresetLabel_;
    juce::ComboBox latencyPresetCombo_;
    juce::TextButton measureLoopbackButton_ {"Measure loopback latency…"};
    juce::Label measuredLatencyLabel_;
    juce::Label offlinePhraseLabel_;
    juce::Label offlinePhonemesLabel_;
    juce::TextEditor offlinePhonemesEditor_;
    juce::Label offlineNoteLabel_;
    juce::TextEditor offlineNoteEditor_;
    juce::Label e2eLabel_;
    juce::Label e2eValueLabel_;
    juce::Label breakdownLabel_;
    juce::TextEditor breakdownEditor_;
    juce::Label livePipelineLabel_;
    juce::ToggleButton liveOnnxToggle_ {"ONNX stub (identity)"};
    juce::TextEditor livePipelineLog_;

    Voice2VocalSynth::AnalysisLatencySettings analysisSettings_ =
        Voice2VocalSynth::LatencyBudgetCalculator::presetSettings(Voice2VocalSynth::LatencyPreset::Balanced);

    std::mutex liveAudioMutex_;
    std::vector<float> liveMonoTail_;
    std::atomic<std::uint64_t> liveSamplesSeen_{0};
    double liveSampleRateHz_ = 48000.0;
    Voice2VocalSynth::RecentPitchTracker pitchTracker_;
    Voice2VocalSynth::PitchTargetCalculator pitchCalculator_;
    Voice2VocalSynth::PhonemeTemporalStabilizer phonemeStabilizer_;
    Voice2VocalSynth::VoiceActivityDetector voiceVad_;
    Voice2VocalSynth::WhistleDetector whistleDetector_;
    Voice2VocalSynth::InferenceLatencyTracker inferenceLatency_;
    Voice2VocalSynth::UtteranceSustainReleasePolicy sustainRelease_;
    Voice2VocalSynth::LoopbackLatencyMeasurer loopbackMeasurer_;
    bool whistleModeActive_ = false;
    int phonemeThrottleCounter_ = 0;
    std::deque<std::string> liveLogLines_;
    bool loopbackResultLogged_ = false;

#if defined(VOICE2VOCALSYNTH_WITH_ONNX)
    std::unique_ptr<Voice2VocalSynth::PhonemeOnnxAsyncRunner> phonemeAsync_;
#endif

    static constexpr int kMaxLiveLogLines = 64;
};
