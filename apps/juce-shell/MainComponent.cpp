#include "MainComponent.h"

#include "Voice2VocalSynth/OfflineRenderer.h"
#include "Voice2VocalSynth/PcmWavWriter.h"
#include "Voice2VocalSynth/PitchTarget.h"
#include "Voice2VocalSynth/RenderPlanner.h"
#include "Voice2VocalSynth/SimplePitchEstimator.h"
#include "Voice2VocalSynth/EvalDataPaths.h"
#include "Voice2VocalSynth/LivePhonemeVerifyPaths.h"
#include "Voice2VocalSynth/PhonemeMappingConfigLoader.h"
#include "Voice2VocalSynth/DebugTimeline.h"
#include "Voice2VocalSynth/AppSettings.h"
#include "Voice2VocalSynth/VoicebankMappingPlanner.h"
#include "Voice2VocalSynth/InferenceLatencyTracker.h"
#include "Voice2VocalSynth/LoopbackLatencyMeasurer.h"
#include "Voice2VocalSynth/MeasuredLatency.h"
#include "Voice2VocalSynth/PlaybackBoundaryMapper.h"
#include "Voice2VocalSynth/UtteranceSustainReleasePolicy.h"
#include "Voice2VocalSynth/VoiceActivityDetector.h"
#include "Voice2VocalSynth/VoicebankScanner.h"
#include "Voice2VocalSynth/WhistleDetector.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

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

[[nodiscard]] std::vector<std::string> parseArpabetTokens(const juce::String& text)
{
    std::vector<std::string> out;
    juce::StringArray parts;
    parts.addTokens(text.trim(), " \t\r\n", "");
    for (const auto& p : parts) {
        if (p.isNotEmpty()) {
            out.push_back(p.toStdString());
        }
    }
    if (out.empty()) {
        out = {"K", "AE", "T"};
    }
    return out;
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

std::string breakdownText(const Voice2VocalSynth::MeasuredLatencySummary& summary)
{
    const auto& bd = summary.estimated;
    std::ostringstream out;
    out.setf(std::ios::fixed, std::ios::floatfield);
    out.precision(2);
    for (const auto& c : bd.components()) {
        out << c.name << ": " << c.milliseconds << " ms\n";
    }
    out << "---\n";
    out << "End-to-end (monitoring estimate): " << bd.endToEndMonitoringLatencyMs() << " ms\n";
    if (summary.has_loopback()) {
        out << "Measured loopback round-trip: " << summary.loopback.round_trip_ms << " ms (corr "
            << summary.loopback.correlation << ", lag " << summary.loopback.lag_samples << " samples)\n";
        out << "Residual (measured - estimate): " << summary.loopback_residual_ms() << " ms\n";
        out << "Playback mapping uses measured E2E + ONNX jitter: " << summary.playback_mapping_ms()
            << " ms\n";
    }
    return out.str();
}

std::string liveTimelineLogLine(const std::string& arpabet, const std::string& timelineJson)
{
    std::string compact;
    compact.reserve(timelineJson.size());
    for (const char character : timelineJson) {
        if (character != '\n') {
            compact.push_back(character);
        }
    }
    const auto insertAt = compact.find('{');
    if (insertAt != std::string::npos) {
        compact.insert(insertAt + 1, "\"kind\":\"live_timeline\",\"arpabet\":\"" + arpabet + "\",");
    }
    return compact;
}

} // namespace

MainComponent::MainComponent(
    std::optional<Voice2VocalSynth::ShellLiveLogExportPaths> liveLogExportPaths)
    : liveLogExportPaths_(std::move(liveLogExportPaths))
{
    titleLabel_.setText("Voice2VocalSynth — JUCE shell", juce::dontSendNotification);
    titleLabel_.setFont(juce::Font(juce::FontOptions(20.0f, juce::Font::bold)));
    titleLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel_);

    hintLabel_.setText(
        "Live pass-through for I/O testing; the live pipeline log records pitch and optional ONNX "
        "stub runs. Settings: %LOCALAPPDATA%\\Voice2VocalSynth\\ (audio_device.xml, shell_settings.json).",
        juce::dontSendNotification);
    hintLabel_.setFont(juce::Font(juce::FontOptions(14.0f)));
    hintLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(hintLabel_);

    addAndMakeVisible(audioSettingsButton_);
    audioSettingsButton_.onClick = [this] { showAudioSettings(); };

    addAndMakeVisible(offlineRenderButton_);
    offlineRenderButton_.onClick = [this] { offlineRenderTest(); };

    latencyPresetLabel_.setText("Latency preset (analysis budget)", juce::dontSendNotification);
    latencyPresetLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(latencyPresetLabel_);
    addAndMakeVisible(latencyPresetCombo_);

    for (int i = 0; i <= static_cast<int>(Voice2VocalSynth::LatencyPreset::Custom); ++i) {
        const auto preset = static_cast<Voice2VocalSynth::LatencyPreset>(i);
        latencyPresetCombo_.addItem(Voice2VocalSynth::LatencyBudgetCalculator::presetName(preset), i + 1);
    }

    offlinePhraseLabel_.setText("Offline render phrase", juce::dontSendNotification);
    offlinePhraseLabel_.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    addAndMakeVisible(offlinePhraseLabel_);

    offlinePhonemesLabel_.setText("ARPABET", juce::dontSendNotification);
    addAndMakeVisible(offlinePhonemesLabel_);
    offlinePhonemesEditor_.setMultiLine(false);
    offlinePhonemesEditor_.setTextToShowWhenEmpty("e.g. K AE T", juce::Colours::grey);
    addAndMakeVisible(offlinePhonemesEditor_);

    offlineNoteLabel_.setText("Note", juce::dontSendNotification);
    addAndMakeVisible(offlineNoteLabel_);
    offlineNoteEditor_.setMultiLine(false);
    offlineNoteEditor_.setTextToShowWhenEmpty("C4", juce::Colours::grey);
    addAndMakeVisible(offlineNoteEditor_);

    loadShellSettingsFromDisk();
    rebuildActivePhonemeBackend();

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

    measureLoopbackButton_.setTooltip(
        "Route output back to input (loopback cable or virtual device), then run a short "
        "correlation probe to measure round-trip latency and augment the budget estimate.");
    measureLoopbackButton_.onClick = [this] { beginLoopbackMeasurement(); };
    addAndMakeVisible(measureLoopbackButton_);

    measuredLatencyLabel_.setJustificationType(juce::Justification::centredLeft);
    measuredLatencyLabel_.setFont(juce::Font(juce::FontOptions(13.0f)));
    addAndMakeVisible(measuredLatencyLabel_);

    e2eLabel_.setText("Effective monitoring latency", juce::dontSendNotification);
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

    livePipelineLabel_.setText("Live pipeline log (JSON lines)", juce::dontSendNotification);
    livePipelineLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(livePipelineLabel_);

    livePhonemeBackendCombo_.addItem("Placeholder pitch gate", 1);
#if defined(VOICE2VOCALSYNTH_WITH_ONNX)
    livePhonemeBackendCombo_.addItem("ONNX phoneme backend", 2);
#endif
    livePhonemeBackendCombo_.onChange = [this] {
        rebuildActivePhonemeBackend();
        saveShellSettings();
    };
    addAndMakeVisible(livePhonemeBackendCombo_);

    liveOnnxToggle_.setTooltip(
        "When enabled (and this build includes ONNX), enqueue downsampled mono frames into the "
        "repository identity model to exercise async inference timestamps.");
#if !defined(VOICE2VOCALSYNTH_WITH_ONNX)
    liveOnnxToggle_.setEnabled(false);
    liveOnnxToggle_.setToggleState(false, juce::dontSendNotification);
#else
    liveOnnxToggle_.setToggleState(true, juce::dontSendNotification);
#endif
    liveOnnxToggle_.onClick = [this] { saveShellSettings(); };
    addAndMakeVisible(liveOnnxToggle_);

    liveSynthesisToggle_.setTooltip(
        "When enabled and a live voicebank is configured, committed phoneme frames are mapped "
        "and mixed into monitor output.");
    liveSynthesisToggle_.onClick = [this] { saveShellSettings(); };
    addAndMakeVisible(liveSynthesisToggle_);

    chooseLiveVoicebankButton_.onClick = [this] { chooseLiveVoicebank(); };
    addAndMakeVisible(chooseLiveVoicebankButton_);
    liveVoicebankLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(liveVoicebankLabel_);

    livePipelineLog_.setMultiLine(true);
    livePipelineLog_.setReadOnly(true);
    livePipelineLog_.setScrollbarsShown(true);
    livePipelineLog_.setCaretVisible(false);
    livePipelineLog_.setFont(juce::Font(
        juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain)));
    addAndMakeVisible(livePipelineLog_);

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

    setSize(640, 860);
    startTimerHz(15);
    refreshLatencyDisplay();
    initializeLiveLogExport();
}

MainComponent::~MainComponent()
{
    stopTimer();
#if defined(VOICE2VOCALSYNTH_WITH_ONNX)
    if (phonemeAsync_) {
        phonemeAsync_->stop();
        phonemeAsync_.reset();
    }
#endif
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
    buttonRow.removeFromLeft(10);
    offlineRenderButton_.setBounds(buttonRow.removeFromLeft(220));
    r.removeFromTop(8);
    latencyPresetLabel_.setBounds(r.removeFromTop(20));
    auto presetRow = r.removeFromTop(28);
    latencyPresetCombo_.setBounds(presetRow.removeFromLeft(320));
    r.removeFromTop(8);
    measureLoopbackButton_.setBounds(r.removeFromTop(28));
    r.removeFromTop(4);
    measuredLatencyLabel_.setBounds(r.removeFromTop(36));
    r.removeFromTop(10);

    offlinePhraseLabel_.setBounds(r.removeFromTop(20));
    auto phraseRow = r.removeFromTop(26);
    offlinePhonemesLabel_.setBounds(phraseRow.removeFromLeft(72));
    offlinePhonemesEditor_.setBounds(phraseRow.removeFromLeft(260));
    phraseRow.removeFromLeft(10);
    offlineNoteLabel_.setBounds(phraseRow.removeFromLeft(40));
    offlineNoteEditor_.setBounds(phraseRow.removeFromLeft(64));
    r.removeFromTop(12);

    e2eLabel_.setBounds(r.removeFromTop(20));
    e2eValueLabel_.setBounds(r.removeFromTop(26));
    r.removeFromTop(8);
    breakdownLabel_.setBounds(r.removeFromTop(20));
    breakdownEditor_.setBounds(r.removeFromTop(200));
    r.removeFromTop(8);
    livePipelineLabel_.setBounds(r.removeFromTop(20));
    auto backendRow = r.removeFromTop(26);
    livePhonemeBackendCombo_.setBounds(backendRow.removeFromLeft(280));
    backendRow.removeFromLeft(10);
    liveOnnxToggle_.setBounds(backendRow);
    r.removeFromTop(6);
    auto synthRow = r.removeFromTop(26);
    liveSynthesisToggle_.setBounds(synthRow.removeFromLeft(220));
    synthRow.removeFromLeft(10);
    chooseLiveVoicebankButton_.setBounds(synthRow.removeFromLeft(160));
    r.removeFromTop(4);
    liveVoicebankLabel_.setBounds(r.removeFromTop(22));
    r.removeFromTop(6);
    livePipelineLog_.setBounds(r);
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

    if (liveSynthesisToggle_.getToggleState() && liveRenderer_.configured() && liveSampleRateHz_ > 0.0 &&
        outputChannelData[0] != nullptr) {
        const std::uint64_t playbackSample = livePlaybackSamples_.load(std::memory_order_relaxed);
        const double playbackSec = static_cast<double>(playbackSample) / liveSampleRateHz_;
        std::vector<float> synth(static_cast<std::size_t>(numSamples), 0.0F);
        liveRenderer_.renderBlock(synth.data(), numSamples, playbackSec, liveSampleRateHz_);
        for (int i = 0; i < numSamples; ++i) {
            outputChannelData[0][i] = 0.5F * outputChannelData[0][i] + synth[static_cast<std::size_t>(i)];
        }
        livePlaybackSamples_.fetch_add(static_cast<std::uint64_t>(numSamples), std::memory_order_relaxed);
    }

    std::vector<float> mono(static_cast<std::size_t>(numSamples), 0.0F);
    if (inputChannelData != nullptr && numInputChannels > 0) {
        const float* ch0 = inputChannelData[0];
        if (ch0 != nullptr) {
            for (int i = 0; i < numSamples; ++i) {
                mono[static_cast<std::size_t>(i)] = ch0[i];
            }
        }
        if (numInputChannels > 1 && inputChannelData[1] != nullptr) {
            const float* ch1 = inputChannelData[1];
            for (int i = 0; i < numSamples; ++i) {
                mono[static_cast<std::size_t>(i)] =
                    0.5F * (mono[static_cast<std::size_t>(i)] + ch1[i]);
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(liveAudioMutex_);
        liveMonoTail_.insert(liveMonoTail_.end(), mono.begin(), mono.end());
        constexpr std::size_t kMaxTail = 96000;
        if (liveMonoTail_.size() > kMaxTail) {
            liveMonoTail_.erase(liveMonoTail_.begin(),
                                liveMonoTail_.begin() + static_cast<std::ptrdiff_t>(liveMonoTail_.size() - kMaxTail));
        }
    }
    liveSamplesSeen_.fetch_add(static_cast<std::uint64_t>(numSamples), std::memory_order_relaxed);

    if (loopbackMeasurer_.is_measuring() && outputChannelData[0] != nullptr && liveSampleRateHz_ > 0.0) {
        loopbackMeasurer_.process(outputChannelData[0], mono.data(), numSamples, liveSampleRateHz_);
    }
}

void MainComponent::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    juce::ignoreUnused(device);
    {
        std::lock_guard<std::mutex> lock(liveAudioMutex_);
        liveMonoTail_.clear();
    }
    liveSamplesSeen_.store(0, std::memory_order_relaxed);
    livePlaybackSamples_.store(0, std::memory_order_relaxed);
    phonemeThrottleCounter_ = 0;
    pitchTracker_.clear();
    phonemeStabilizer_.reset();
    voiceVad_.reset();
    whistleDetector_.reset();
    inferenceLatency_.reset();
    sustainRelease_.reset();
    loopbackMeasurer_.reset();
    loopbackResultLogged_ = false;
    whistleModeActive_ = false;
    liveRenderer_.reset();
    if (device != nullptr) {
        liveSampleRateHz_ = device->getCurrentSampleRate();
        logDeviceSettings(device);
    }

#if defined(VOICE2VOCALSYNTH_WITH_ONNX)
    phonemeAsync_ = std::make_unique<Voice2VocalSynth::PhonemeOnnxAsyncRunner>();
    {
        std::string err;
        const std::filesystem::path model{VOICE2VOCALSYNTH_REPOSITORY_PHONEME_ONNX_FIXTURE};
        if (!phonemeAsync_->start(model, err)) {
            phonemeAsync_.reset();
            juce::Logger::writeToLog("Voice2VocalSynth: ONNX async start failed: " + juce::String(err.c_str()));
        }
    }
#endif
}

void MainComponent::audioDeviceStopped()
{
#if defined(VOICE2VOCALSYNTH_WITH_ONNX)
    if (phonemeAsync_) {
        phonemeAsync_->stop();
        phonemeAsync_.reset();
    }
#endif
}

void MainComponent::timerCallback()
{
    refreshLatencyDisplay();
    livePipelineTimerTick();
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
    obj->setProperty("offlinePhonemes", offlinePhonemesEditor_.getText());
    obj->setProperty("offlineNote", offlineNoteEditor_.getText());
    obj->setProperty("liveOnnxStub", liveOnnxToggle_.getToggleState());
    obj->setProperty("livePhonemeBackend",
                      livePhonemeBackendCombo_.getSelectedId() == 2 ? "onnx_phoneme" : "placeholder");
    obj->setProperty("liveSynthesisEnabled", liveSynthesisToggle_.getToggleState());
    obj->setProperty("liveVoicebankPath", liveVoicebankPath_);
    obj->setProperty("evalDataFolder", evalDataFolderPath_);
    obj->setProperty("phonemeMappingPath", phonemeMappingPath_);
    obj->setProperty("phonemeOnnxUseRepositoryFixture", phonemeOnnxUseRepositoryFixture_);
    obj->setProperty("phonemeOnnxModelPath", phonemeOnnxModelPath_);
    obj->setProperty("phonemeOnnxConfigPath", phonemeOnnxConfigPath_);

    const auto f = shellSettingsFile();
    (void)f.getParentDirectory().createDirectory();
    const juce::String text = juce::JSON::toString(juce::var(obj.get()), false);
    if (!f.replaceWithText(text)) {
        juce::Logger::writeToLog("Voice2VocalSynth: failed to write " + f.getFullPathName());
    }
}

void MainComponent::loadShellSettingsFromDisk()
{
    latencyPresetCombo_.setSelectedId(static_cast<int>(Voice2VocalSynth::LatencyPreset::Balanced) + 1,
                                     juce::dontSendNotification);
    analysisSettings_ = Voice2VocalSynth::LatencyBudgetCalculator::presetSettings(
        Voice2VocalSynth::LatencyPreset::Balanced);
    offlinePhonemesEditor_.setText("K AE T", juce::dontSendNotification);
    offlineNoteEditor_.setText("C4", juce::dontSendNotification);

    const auto f = shellSettingsFile();
    if (!f.existsAsFile()) {
        evalDataFolderPath_ = juce::String(Voice2VocalSynth::defaultEvalDataRoot().string());
        std::string layoutError;
        (void)Voice2VocalSynth::ensureEvalDataLayout(
            std::filesystem::path(evalDataFolderPath_.toStdString()), layoutError);
        liveVoicebankLabel_.setText("Live voicebank: (not set)", juce::dontSendNotification);
        return;
    }

    const juce::var parsed = juce::JSON::parse(f.loadFileAsString());
    if (parsed.isVoid()) {
        return;
    }

    if (parsed.hasProperty("latencyPreset")) {
        const int v = juce::jlimit(
            0,
            static_cast<int>(Voice2VocalSynth::LatencyPreset::Custom),
            static_cast<int>(parsed.getProperty("latencyPreset", 0)));
        latencyPresetCombo_.setSelectedId(v + 1, juce::dontSendNotification);
        analysisSettings_ = Voice2VocalSynth::LatencyBudgetCalculator::presetSettings(
            static_cast<Voice2VocalSynth::LatencyPreset>(v));
    }

    if (parsed.hasProperty("offlinePhonemes")) {
        const juce::String s = parsed.getProperty("offlinePhonemes", {}).toString();
        if (s.isNotEmpty()) {
            offlinePhonemesEditor_.setText(s, juce::dontSendNotification);
        }
    }
    if (parsed.hasProperty("offlineNote")) {
        const juce::String s = parsed.getProperty("offlineNote", {}).toString();
        if (s.isNotEmpty()) {
            offlineNoteEditor_.setText(s, juce::dontSendNotification);
        }
    }
    if (parsed.hasProperty("liveOnnxStub")) {
        liveOnnxToggle_.setToggleState(static_cast<bool>(parsed.getProperty("liveOnnxStub", true)),
                                       juce::dontSendNotification);
    }
    if (parsed.hasProperty("livePhonemeBackend")) {
        const juce::String backend = parsed.getProperty("livePhonemeBackend", "placeholder").toString();
        livePhonemeBackendCombo_.setSelectedId(backend == "onnx_phoneme" ? 2 : 1, juce::dontSendNotification);
    }
    if (parsed.hasProperty("liveSynthesisEnabled")) {
        liveSynthesisToggle_.setToggleState(static_cast<bool>(parsed.getProperty("liveSynthesisEnabled", false)),
                                            juce::dontSendNotification);
    }
    if (parsed.hasProperty("liveVoicebankPath")) {
        liveVoicebankPath_ = parsed.getProperty("liveVoicebankPath", {}).toString();
    }
    if (parsed.hasProperty("evalDataFolder")) {
        evalDataFolderPath_ = parsed.getProperty("evalDataFolder", {}).toString();
    } else {
        evalDataFolderPath_ = juce::String(Voice2VocalSynth::defaultEvalDataRoot().string());
    }
    if (parsed.hasProperty("phonemeMappingPath")) {
        phonemeMappingPath_ = parsed.getProperty("phonemeMappingPath", {}).toString();
    }
    if (parsed.hasProperty("phonemeOnnxUseRepositoryFixture")) {
        phonemeOnnxUseRepositoryFixture_ =
            static_cast<bool>(parsed.getProperty("phonemeOnnxUseRepositoryFixture", true));
    }
    if (parsed.hasProperty("phonemeOnnxModelPath")) {
        phonemeOnnxModelPath_ = parsed.getProperty("phonemeOnnxModelPath", {}).toString();
    }
    if (parsed.hasProperty("phonemeOnnxConfigPath")) {
        phonemeOnnxConfigPath_ = parsed.getProperty("phonemeOnnxConfigPath", {}).toString();
    }

    {
        std::string layoutError;
        const auto evalRoot = std::filesystem::path(evalDataFolderPath_.toStdString());
        (void)Voice2VocalSynth::ensureEvalDataLayout(evalRoot, layoutError);
    }

    if (liveVoicebankPath_.isNotEmpty()) {
        configureLiveRenderer();
        liveVoicebankLabel_.setText("Live voicebank: " + liveVoicebankPath_, juce::dontSendNotification);
    } else {
        liveVoicebankLabel_.setText("Live voicebank: (not set)", juce::dontSendNotification);
    }
}

void MainComponent::pushLiveLogLine(const std::string& line)
{
    liveLogLines_.push_back(line);
    while (liveLogLines_.size() > static_cast<std::size_t>(kMaxLiveLogLines)) {
        liveLogLines_.pop_front();
    }
    juce::String text;
    for (const auto& entry : liveLogLines_) {
        text << juce::String(entry) << "\n";
    }
    livePipelineLog_.setText(text);
    appendLiveLogExportLine(line);
}

void MainComponent::initializeLiveLogExport()
{
    if (!liveLogExportPaths_) {
        return;
    }

    std::string error;
    if (!writeLivePhonemeVerifyManifest(liveLogExportPaths_->runPaths, error)) {
        juce::Logger::writeToLog("Voice2VocalSynth: manifest write failed: " + juce::String(error));
        return;
    }

    liveLogFile_ = std::make_unique<std::ofstream>(liveLogExportPaths_->runPaths.liveLog,
                                                   std::ios::binary | std::ios::app);
    if (!liveLogFile_->is_open()) {
        juce::Logger::writeToLog("Voice2VocalSynth: unable to open live log export: "
                                 + juce::String(liveLogExportPaths_->runPaths.liveLog.string()));
        liveLogFile_.reset();
        return;
    }

    liveLogExportReady_ = true;
    const char* backendName = activePhonemeBackend() ? activePhonemeBackend()->name() : "none";
    std::ostringstream session;
    session << "{\"kind\":\"session_start\",\"live_log_export\":true"
            << ",\"run_directory\":\"" << liveLogExportPaths_->runPaths.runDirectory.string() << "\""
            << ",\"manifest\":\"" << liveLogExportPaths_->runPaths.manifest.string() << "\""
            << ",\"backend\":\"" << backendName << "\"}";
    pushLiveLogLine(session.str());
}

void MainComponent::appendLiveLogExportLine(const std::string& line)
{
    if (!liveLogExportReady_ || liveLogFile_ == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(liveLogFileMutex_);
    (*liveLogFile_) << line << '\n';
    liveLogFile_->flush();
}

void MainComponent::logDeviceSettings(juce::AudioIODevice* device)
{
    if (device == nullptr) {
        return;
    }

    const auto latency = deviceLatencyFromJuce(device);
    std::ostringstream settings;
    settings.setf(std::ios::fixed);
    settings.precision(3);
    settings << "{\"kind\":\"device_settings\""
             << ",\"sample_rate_hz\":" << latency.sampleRateHz
             << ",\"buffer_samples\":" << device->getCurrentBufferSizeSamples()
             << ",\"input_latency_samples\":" << latency.inputDeviceLatencySamples
             << ",\"output_latency_samples\":" << latency.outputDeviceLatencySamples
             << ",\"input_buffer_samples\":" << latency.inputBufferSizeSamples
             << ",\"output_buffer_samples\":" << latency.outputBufferSizeSamples << "}";
    pushLiveLogLine(settings.str());
}

void MainComponent::livePipelineTimerTick()
{
    std::vector<float> tailCopy;
    const std::uint64_t samples = liveSamplesSeen_.load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(liveAudioMutex_);
        tailCopy = liveMonoTail_;
    }
    if (liveSampleRateHz_ <= 0.0) {
        return;
    }

    const int n = static_cast<int>(tailCopy.size());
    if (n < Voice2VocalSynth::simplePitchEstimatorMinSamples()) {
        return;
    }

    constexpr int kPitchWindow = 4096;
    const int offset = juce::jmax(0, n - kPitchWindow);
    const float* ptr = tailCopy.data() + offset;
    const int win = n - offset;
    const auto est = Voice2VocalSynth::estimatePitchFromMono(ptr, win, liveSampleRateHz_);
    const double streamSec = static_cast<double>(samples) / liveSampleRateHz_;

    const float rms = Voice2VocalSynth::VoiceActivityDetector::rms_from_mono(ptr, win);
    voiceVad_.observe_rms(rms, streamSec);

    Voice2VocalSynth::AudioDeviceLatency deviceLat =
        deviceLatencyFromJuce(deviceManager.getCurrentAudioDevice());
    const auto latencyBreakdown =
        Voice2VocalSynth::LatencyBudgetCalculator::calculate(deviceLat, analysisSettings_);
    const double inferenceJitterMs = inferenceLatency_.has_estimate() ? inferenceLatency_.estimate_ms() : 0.0;
    const double measuredE2eMs = measuredEndToEndMsForMapping();

    if (loopbackMeasurer_.has_result() && !loopbackResultLogged_) {
        const auto m = loopbackMeasurer_.result();
        std::ostringstream ml;
        ml.setf(std::ios::fixed);
        ml.precision(3);
        ml << "{\"kind\":\"latency_measure\",\"valid\":" << (m.valid ? "true" : "false")
           << ",\"round_trip_ms\":" << m.round_trip_ms << ",\"corr\":" << m.correlation
           << ",\"lag_samples\":" << m.lag_samples << ",\"estimate_ms\":"
           << latencyBreakdown.endToEndMonitoringLatencyMs() << "}";
        pushLiveLogLine(ml.str());
        loopbackResultLogged_ = true;
        refreshLatencyDisplay();
    }

    Voice2VocalSynth::PitchInput pin;
    pin.frequencyHz = est.frequencyHz;
    pin.confidence = est.confidence;
    pitchTracker_.addInput(pin, streamSec);
    const auto smoothed = pitchTracker_.withRecentMean(pin, streamSec);
    const auto target = pitchCalculator_.calculate(smoothed);

    std::ostringstream line;
    line.setf(std::ios::fixed);
    line.precision(5);
    line << "{\"kind\":\"pitch\",\"t\":" << streamSec << ",\"f0_hz\":" << est.frequencyHz
         << ",\"conf\":" << est.confidence << ",\"target_midi\":" << target.targetMidi << "}";

    Voice2VocalSynth::SpeechBoundaryEvent vadEv;
    while (voiceVad_.try_pop_boundary(vadEv)) {
        const double playbackT = Voice2VocalSynth::PlaybackBoundaryMapper::analysisToPlaybackSeconds(
            vadEv.stream_time_seconds, latencyBreakdown, inferenceJitterMs, measuredE2eMs);
        sustainRelease_.on_speech_boundary(vadEv, latencyBreakdown, inferenceJitterMs);
        if (vadEv.kind == Voice2VocalSynth::SpeechBoundaryKind::Onset) {
            liveRenderer_.onUtteranceStart();
        }
        std::ostringstream vl;
        vl.setf(std::ios::fixed);
        vl.precision(5);
        vl << "{\"kind\":\"vad\",\"event\":\"" << Voice2VocalSynth::speechBoundaryKindName(vadEv.kind)
           << "\",\"t\":" << vadEv.stream_time_seconds << ",\"rms\":" << vadEv.rms
           << ",\"t_playback\":" << playbackT << "}";
        pushLiveLogLine(vl.str());
    }

    const double playbackNow = Voice2VocalSynth::PlaybackBoundaryMapper::analysisToPlaybackSeconds(
        streamSec, latencyBreakdown, inferenceJitterMs, measuredE2eMs);
    Voice2VocalSynth::SustainReleaseCommand releaseCmd;
    while (sustainRelease_.try_pop_release(playbackNow, releaseCmd)) {
        liveRenderer_.onSustainRelease(releaseCmd.playback_time_seconds);
        std::ostringstream rl;
        rl.setf(std::ios::fixed);
        rl.precision(5);
        rl << "{\"kind\":\"sustain_release\",\"t_playback\":" << releaseCmd.playback_time_seconds
           << ",\"t_analysis_end\":" << releaseCmd.analysis_end_seconds << "}";
        pushLiveLogLine(rl.str());
    }

    const auto whistleObservation =
        whistleDetector_.analyze(ptr, win, liveSampleRateHz_, streamSec, est);
    whistleDetector_.observe(whistleObservation);
    whistleModeActive_ = whistleDetector_.is_active();
    if (whistleObservation.is_whistle) {
        std::ostringstream wl;
        wl.setf(std::ios::fixed);
        wl.precision(5);
        wl << "{\"kind\":\"whistle\",\"t\":" << streamSec << ",\"active\":" << (whistleModeActive_ ? "true" : "false")
           << ",\"conf\":" << whistleObservation.confidence << ",\"f0_hz\":" << whistleObservation.f0_hz
           << ",\"hnr_ratio\":" << whistleObservation.harmonic_energy_ratio
           << ",\"peak_ratio\":" << whistleObservation.narrowband_peak_ratio << "}";
        pushLiveLogLine(wl.str());
    }

    Voice2VocalSynth::WhistleBoundaryEvent whistleBoundary;
    while (whistleDetector_.try_pop_boundary(whistleBoundary)) {
        std::ostringstream wel;
        wel.setf(std::ios::fixed);
        wel.precision(5);
        wel << "{\"kind\":\"whistle_edge\",\"event\":\"" << (whistleBoundary.active ? "onset" : "end")
            << "\",\"t\":" << whistleBoundary.stream_time_seconds << ",\"conf\":" << whistleBoundary.confidence
            << ",\"f0_hz\":" << whistleBoundary.f0_hz << "}";
        pushLiveLogLine(wel.str());
    }

    // Hybrid testing path (intentional): pitch-gated placeholder hypotheses feed the temporal
    // stabilizer so `ph_frame` commits can be exercised without a real phoneme ONNX head. The
    // ONNX identity stub below remains a separate async pipeline for inference/timing checks.
    // Whistle mode bypasses phoneme hypotheses (`whistleDetection.separatePitchPath`).
    if (!whistleModeActive_) {
        Voice2VocalSynth::PhonemeBackendAudioFrame backendFrame;
        backendFrame.monoSamples.assign(ptr, ptr + win);
        backendFrame.sampleRateHz = liveSampleRateHz_;
        backendFrame.streamTimeStartSeconds = streamSec - (static_cast<double>(win) / liveSampleRateHz_);
        if (auto* backend = activePhonemeBackend()) {
            const auto backendResult = backend->process(backendFrame);
            if (backendResult.ok) {
                for (const auto& ob : backendResult.observations) {
                    phonemeStabilizer_.observe(ob);
                }
            }
        }
    }
    Voice2VocalSynth::PhonemeFrame phFrame;
    while (phonemeStabilizer_.try_pop_committed(phFrame)) {
        if (liveSynthesisToggle_.getToggleState() && liveRenderer_.configured()) {
            liveRenderer_.onCommittedPhoneme(phFrame,
                                            target,
                                            playbackNow,
                                            latencyBreakdown.endToEndMonitoringLatencyMs());
            if (const auto& timeline = liveRenderer_.lastTimeline()) {
                pushLiveLogLine(liveTimelineLogLine(phFrame.arpabet,
                                                    Voice2VocalSynth::DebugTimelineExporter::toJson(*timeline)));
            }
        }
        std::ostringstream cl;
        cl.setf(std::ios::fixed);
        cl.precision(5);
        const char* backendName = activePhonemeBackend() ? activePhonemeBackend()->name() : "none";
        const auto emissionSteadyNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count();
        cl << "{\"kind\":\"ph_frame\",\"backend\":\"" << backendName << "\",\"arpabet\":\"" << phFrame.arpabet
           << "\",\"conf\":" << phFrame.confidence << ",\"t0\":" << phFrame.estimatedOnsetSeconds
           << ",\"t1\":" << phFrame.estimatedEndSeconds << ",\"steady_ns\":" << emissionSteadyNs
           << ",\"vowel\":" << (phFrame.isVowel ? "true" : "false")
           << ",\"consonant\":" << (phFrame.isConsonant ? "true" : "false") << "}";
        pushLiveLogLine(cl.str());
    }

#if defined(VOICE2VOCALSYNTH_WITH_ONNX)
    Voice2VocalSynth::PhonemeOnnxAsyncJobOutput phonOut;
    while (phonemeAsync_ && phonemeAsync_->try_pop_completed(phonOut)) {
        std::ostringstream pl;
        pl.setf(std::ios::fixed);
        pl.precision(5);
        const auto lagNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            phonOut.inference_completed_steady_time - phonOut.enqueued_steady_time).count();
        const double lagMs = static_cast<double>(lagNs) / 1.0e6;
        inferenceLatency_.observe_ms(lagMs);
        pl << "{\"kind\":\"onnx\",\"job\":" << phonOut.job_id << ",\"t_stream\":" << phonOut.stream_time_start_seconds
           << ",\"steady_ns\":" << std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     phonOut.inference_completed_steady_time.time_since_epoch())
                                     .count()
           << ",\"ok\":" << (phonOut.run.ok ? "true" : "false") << ",\"out_elems\":" << phonOut.run.output.size()
           << ",\"lag_ms\":" << lagMs << ",\"lag_est_ms\":" << inferenceLatency_.estimate_ms()
           << "}";
        pushLiveLogLine(pl.str());
    }

    if (liveOnnxToggle_.getToggleState() && phonemeAsync_ && phonemeAsync_->running()) {
        if (++phonemeThrottleCounter_ >= 6) {
            phonemeThrottleCounter_ = 0;
            constexpr int kSource = 256;
            constexpr int kOnnx = 32;
            const int have = juce::jmin(kSource, n);
            if (have >= kOnnx) {
                const float* src = tailCopy.data() + (n - have);
                const int per = have / kOnnx;
                if (per > 0) {
                    std::vector<float> onnxIn(static_cast<std::size_t>(kOnnx));
                    for (int i = 0; i < kOnnx; ++i) {
                        double acc = 0.0;
                        for (int k = 0; k < per; ++k) {
                            acc += static_cast<double>(src[i * per + k]);
                        }
                        onnxIn[static_cast<std::size_t>(i)] = static_cast<float>(acc / static_cast<double>(per));
                    }
                    Voice2VocalSynth::PhonemeOnnxAsyncJobInput job;
                    job.stream_time_start_seconds = streamSec;
                    job.features = std::move(onnxIn);
                    (void)phonemeAsync_->enqueue(std::move(job));
                }
            }
        }
    }
#endif

    pushLiveLogLine(line.str());
}

void MainComponent::offlineRenderTest()
{
    saveShellSettings();

    const auto folderFlags = juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectDirectories;
    auto dirChooser = std::make_shared<juce::FileChooser>("Select voicebank folder", juce::File {}, "*");
    dirChooser->launchAsync(folderFlags, [this, dirChooser](const juce::FileChooser& fc) {
        juce::ignoreUnused(dirChooser);
        const juce::File dir = fc.getResult();
        if (!dir.isDirectory()) {
            return;
        }

        auto saveChooser = std::make_shared<juce::FileChooser>(
            "Save WAV as",
            dir.getChildFile("Voice2VocalSynth_offline_test.wav"),
            "*.wav");
        const auto saveFlags = juce::FileBrowserComponent::saveMode
                               | juce::FileBrowserComponent::warnAboutOverwriting;
        saveChooser->launchAsync(saveFlags, [this, dir, saveChooser](const juce::FileChooser& fc2) {
            juce::ignoreUnused(saveChooser);
            juce::File out = fc2.getResult();
            if (out.getFullPathName().isEmpty()) {
                return;
            }
            if (!out.hasFileExtension(".wav")) {
                out = out.withFileExtension("wav");
            }
            runOfflineRenderToFile(dir, out);
        });
    });
}

void MainComponent::runOfflineRenderToFile(const juce::File& voicebankDirectory,
                                           const juce::File& outputWavFile)
{
    saveShellSettings();

    const std::filesystem::path root { voicebankDirectory.getFullPathName().toStdString() };

    const auto scan = Voice2VocalSynth::VoicebankScanner::scan(root);
    if (!scan.foundOtoIni()) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                "Offline render",
                                                "No oto.ini found under the selected folder.");
        return;
    }

    std::optional<std::filesystem::path> mappingPath;
    {
        const auto shell = shellSettingsFile();
        if (shell.existsAsFile()) {
            const juce::var parsed = juce::JSON::parse(shell.loadFileAsString());
            if (parsed.hasProperty("phonemeMappingPath")) {
                const juce::String p = parsed.getProperty("phonemeMappingPath", {}).toString();
                if (p.isNotEmpty()) {
                    mappingPath = std::filesystem::path(p.toStdString());
                }
            }
        }
        if (!mappingPath) {
            const juce::File userMap = appDataRootDirectory().getChildFile("phoneme_to_japanese.json");
            if (userMap.existsAsFile()) {
                mappingPath = std::filesystem::path(userMap.getFullPathName().toStdString());
            }
        }
    }
    const auto mappingLoad = Voice2VocalSynth::PhonemeMappingConfigLoader::loadEffective(mappingPath);
    Voice2VocalSynth::VoicebankMappingPlanner planner(
        Voice2VocalSynth::PhonemeFallbackMapper(mappingLoad.options));
    Voice2VocalSynth::VoicebankMappingRequest mapReq;
    mapReq.arpabetPhonemes = parseArpabetTokens(offlinePhonemesEditor_.getText());
    juce::String note = offlineNoteEditor_.getText().trim();
    if (note.isEmpty()) {
        note = "C4";
    }
    mapReq.targetNoteName = note.toStdString();
    const auto mapping = planner.plan(mapReq, scan.aliasIndex, scan.prefixMapEntries);

    Voice2VocalSynth::PitchTargetCalculator calculator;
    Voice2VocalSynth::PitchInput pitchIn;
    pitchIn.frequencyHz = Voice2VocalSynth::PitchTargetCalculator::midiToFrequency(60.0);
    pitchIn.confidence = 1.0;
    const auto pitchTarget = calculator.calculate(pitchIn);

    Voice2VocalSynth::RenderPlanRequest planReq;
    planReq.mappingPlan = mapping;
    planReq.pitchTarget = pitchTarget;
    planReq.startTimeSeconds = 0.0;
    planReq.defaultEventDurationMs = 120.0;
    const auto renderPlan = Voice2VocalSynth::RenderPlanner::plan(planReq);

    Voice2VocalSynth::OfflineRenderOptions opts;
    opts.voicebankRoot = root;
    opts.outputSampleRate = 48000;
    if (scan.hasBankRootRecordingPitch()) {
        opts.defaultSourceRecordingFrequencyHz = scan.bankRootRecordingFrequencyHz;
    }
    auto rendered = Voice2VocalSynth::OfflineRenderer::render(renderPlan, opts);

    if (!rendered.ok) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                "Offline render",
                                                juce::String(rendered.error));
        return;
    }

    const std::filesystem::path outPath { outputWavFile.getFullPathName().toStdString() };
    const auto writeRes =
        Voice2VocalSynth::PcmWavWriter::writeMonoPcm16(outPath, rendered.mono, rendered.sampleRate);
    if (!writeRes.ok) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                "Offline render",
                                                juce::String(writeRes.error));
        return;
    }

    juce::String msg = "Saved: " + outputWavFile.getFullPathName() + "\n\n";
    msg += "Render events: " + juce::String(static_cast<int>(renderPlan.events.size())) + "\n";
    if (!renderPlan.skippedEvents.empty()) {
        msg += "Skipped: " + juce::String(static_cast<int>(renderPlan.skippedEvents.size())) + "\n";
    }
    for (const auto& w : rendered.warnings) {
        msg += juce::String(w) + "\n";
    }

    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Offline render", msg);
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
        Voice2VocalSynth::MeasuredLatencySummary summary;
        summary.estimated = bd;
        if (loopbackMeasurer_.has_result()) {
            summary.loopback = loopbackMeasurer_.result();
        }
        summary.inference_jitter_ms = inferenceLatency_.has_estimate() ? inferenceLatency_.estimate_ms() : 0.0;
        breakdownEditor_.setText(breakdownText(summary));
    } catch (...) {
        e2eValueLabel_.setText("(latency calculation error)", juce::dontSendNotification);
        breakdownEditor_.clear();
    }
}

void MainComponent::beginLoopbackMeasurement()
{
    loopbackMeasurer_.begin();
    loopbackResultLogged_ = false;
    measuredLatencyLabel_.setText("Loopback measurement running...", juce::dontSendNotification);
    pushLiveLogLine("{\"kind\":\"latency_measure_start\"}");
}

double MainComponent::measuredEndToEndMsForMapping() const
{
    if (!loopbackMeasurer_.has_result()) {
        return 0.0;
    }

    const auto measurement = loopbackMeasurer_.result();
    return measurement.valid ? measurement.round_trip_ms : 0.0;
}

void MainComponent::rebuildActivePhonemeBackend()
{
#if defined(VOICE2VOCALSYNTH_WITH_ONNX)
    phonemeOnnxBackend_.reset();
    if (livePhonemeBackendCombo_.getSelectedId() == 2) {
        Voice2VocalSynth::PhonemeOnnxBackendOptions options;
        if (phonemeOnnxUseRepositoryFixture_ || phonemeOnnxModelPath_.isEmpty()) {
            options.modelPath = std::filesystem::path{VOICE2VOCALSYNTH_REPOSITORY_PHONEME_ONNX_FIXTURE};
            options.configPath = std::filesystem::path{VOICE2VOCALSYNTH_REPOSITORY_ROOT} /
                                 "tests/fixtures/onnx/dummy_identity.phoneme.json";
        } else {
            options.modelPath = std::filesystem::path(phonemeOnnxModelPath_.toStdString());
            if (phonemeOnnxConfigPath_.isNotEmpty()) {
                options.configPath = std::filesystem::path(phonemeOnnxConfigPath_.toStdString());
            }
        }
        phonemeOnnxBackend_ = std::make_unique<Voice2VocalSynth::PhonemeOnnxBackend>(std::move(options));
        std::string error;
        if (!phonemeOnnxBackend_->load(error)) {
            juce::Logger::writeToLog("Voice2VocalSynth: phoneme ONNX backend load failed: " +
                                     juce::String(error.c_str()));
            phonemeOnnxBackend_.reset();
            livePhonemeBackendCombo_.setSelectedId(1, juce::dontSendNotification);
        }
    }
#else
    juce::ignoreUnused(this);
#endif
}

Voice2VocalSynth::IPhonemeBackend* MainComponent::activePhonemeBackend()
{
#if defined(VOICE2VOCALSYNTH_WITH_ONNX)
    if (livePhonemeBackendCombo_.getSelectedId() == 2 && phonemeOnnxBackend_ && phonemeOnnxBackend_->loaded()) {
        return phonemeOnnxBackend_.get();
    }
#endif
    return &placeholderPhonemeBackend_;
}

void MainComponent::chooseLiveVoicebank()
{
    const auto folderFlags = juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectDirectories;
    juce::File start;
    if (liveVoicebankPath_.isNotEmpty()) {
        start = juce::File(liveVoicebankPath_);
    }
    auto dirChooser = std::make_shared<juce::FileChooser>("Select live voicebank folder", start, "*");
    dirChooser->launchAsync(folderFlags, [this, dirChooser](const juce::FileChooser& fc) {
        juce::ignoreUnused(dirChooser);
        const juce::File dir = fc.getResult();
        if (!dir.isDirectory()) {
            return;
        }

        liveVoicebankPath_ = dir.getFullPathName();
        configureLiveRenderer();
        if (!liveRenderer_.configured()) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                    "Live voicebank",
                                                    "Unable to configure live renderer for the selected voicebank.");
            liveVoicebankPath_.clear();
            liveVoicebankLabel_.setText("Live voicebank: (not set)", juce::dontSendNotification);
            return;
        }

        liveVoicebankLabel_.setText("Live voicebank: " + liveVoicebankPath_, juce::dontSendNotification);
        saveShellSettings();
    });
}

std::optional<std::filesystem::path> MainComponent::shellPhonemeMappingPath() const
{
    if (phonemeMappingPath_.isNotEmpty()) {
        return std::filesystem::path(phonemeMappingPath_.toStdString());
    }

    const juce::File userMap = appDataRootDirectory().getChildFile("phoneme_to_japanese.json");
    if (userMap.existsAsFile()) {
        return std::filesystem::path(userMap.getFullPathName().toStdString());
    }

    return std::nullopt;
}

void MainComponent::configureLiveRenderer()
{
    if (liveVoicebankPath_.isEmpty()) {
        return;
    }

    std::string error;
    (void)liveRenderer_.configure(std::filesystem::path(liveVoicebankPath_.toStdString()),
                                  shellPhonemeMappingPath(),
                                  error);
    if (!error.empty()) {
        juce::Logger::writeToLog("Voice2VocalSynth: live renderer configure failed: " + juce::String(error.c_str()));
    }
}
