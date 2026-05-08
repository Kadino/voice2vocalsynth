#include "MainComponent.h"

#include "Voice2VocalSynth/OfflineRenderer.h"
#include "Voice2VocalSynth/PcmWavWriter.h"
#include "Voice2VocalSynth/PitchTarget.h"
#include "Voice2VocalSynth/RenderPlanner.h"
#include "Voice2VocalSynth/VoicebankMappingPlanner.h"
#include "Voice2VocalSynth/VoicebankScanner.h"

#include <filesystem>
#include <memory>
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

    setSize(640, 600);
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
    buttonRow.removeFromLeft(10);
    offlineRenderButton_.setBounds(buttonRow.removeFromLeft(220));
    r.removeFromTop(8);
    latencyPresetLabel_.setBounds(r.removeFromTop(20));
    auto presetRow = r.removeFromTop(28);
    latencyPresetCombo_.setBounds(presetRow.removeFromLeft(320));
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
    obj->setProperty("offlinePhonemes", offlinePhonemesEditor_.getText());
    obj->setProperty("offlineNote", offlineNoteEditor_.getText());

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
        const juce::String s = parsed.getProperty("offlinePhonemes").toString();
        if (s.isNotEmpty()) {
            offlinePhonemesEditor_.setText(s, juce::dontSendNotification);
        }
    }
    if (parsed.hasProperty("offlineNote")) {
        const juce::String s = parsed.getProperty("offlineNote").toString();
        if (s.isNotEmpty()) {
            offlineNoteEditor_.setText(s, juce::dontSendNotification);
        }
    }
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

    Voice2VocalSynth::VoicebankMappingPlanner planner;
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
        breakdownEditor_.setText(breakdownText(bd));
    } catch (...) {
        e2eValueLabel_.setText("(latency calculation error)", juce::dontSendNotification);
        breakdownEditor_.clear();
    }
}
