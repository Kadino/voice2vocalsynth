#include "Voice2VocalSynth/PocketSphinxPhonemeBackend.h"
#include "Voice2VocalSynth/PhonemeFallbackMapper.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#if defined(VOICE2VOCALSYNTH_WITH_POCKETSPHINX)
#include <pocketsphinx.h>
#endif

namespace Voice2VocalSynth
{
namespace
{

constexpr double kPocketSphinxSampleRateHz = 16000.0;

const std::vector<std::string>& arpabetInventory()
{
    static const std::vector<std::string> labels {
        "sil", "AA", "AE", "AH", "AO", "AW", "AY", "B", "CH", "D", "DH",
        "EH", "ER", "EY", "F", "G", "HH", "IH", "IY", "JH", "K", "L", "M",
        "N", "NG", "OW", "OY", "P", "R", "S", "SH", "T", "TH", "UH", "UW",
        "V", "W", "Y", "Z", "ZH",
    };
    return labels;
}

std::string normalizePhone(std::string phone)
{
    if (!phone.empty() && phone.back() >= '0' && phone.back() <= '2') {
        phone.pop_back();
    }
    if (phone == "SIL" || phone == "<s>" || phone == "</s>" || phone == "+SPN+" ||
        phone == "<sil>") {
        return {};
    }
    return phone;
}

struct StreamingResampler
{
    void reset()
    {
        sourceRateHz = 0.0;
        sourceSamplesSeen = 0;
        nextSourcePosition = 0.0;
        previousSample = 0.0F;
        havePreviousSample = false;
    }

    std::vector<std::int16_t> process(const float* samples,
                                      const std::size_t count,
                                      const double rate)
    {
        if (samples == nullptr || count == 0 || rate <= 0.0) {
            return {};
        }
        if (sourceRateHz <= 0.0 || std::abs(sourceRateHz - rate) > 1.0e-6) {
            reset();
            sourceRateHz = rate;
        }

        const std::uint64_t chunkStart = sourceSamplesSeen;
        const std::uint64_t chunkEnd = chunkStart + count;
        std::vector<std::int16_t> output;
        output.reserve(static_cast<std::size_t>(
            std::ceil(static_cast<double>(count) * kPocketSphinxSampleRateHz / rate)) + 1);

        const double sourceStep = rate / kPocketSphinxSampleRateHz;
        while (std::ceil(nextSourcePosition) < static_cast<double>(chunkEnd)) {
            const auto lowerGlobal = static_cast<std::uint64_t>(std::floor(nextSourcePosition));
            const auto upperGlobal = static_cast<std::uint64_t>(std::ceil(nextSourcePosition));
            const auto sampleAt = [&](const std::uint64_t globalIndex) {
                if (globalIndex < chunkStart) {
                    return previousSample;
                }
                return samples[std::min<std::size_t>(
                    static_cast<std::size_t>(globalIndex - chunkStart), count - 1)];
            };
            const float lower = sampleAt(lowerGlobal);
            const float upper = sampleAt(upperGlobal);
            const float fraction =
                static_cast<float>(nextSourcePosition - std::floor(nextSourcePosition));
            const float value =
                std::clamp(lower * (1.0F - fraction) + upper * fraction, -1.0F, 1.0F);
            output.push_back(
                static_cast<std::int16_t>(std::lround(value * 32767.0F)));
            nextSourcePosition += sourceStep;
        }
        previousSample = samples[count - 1];
        havePreviousSample = true;
        sourceSamplesSeen = chunkEnd;
        return output;
    }

    double sourceRateHz = 0.0;
    std::uint64_t sourceSamplesSeen = 0;
    double nextSourcePosition = 0.0;
    float previousSample = 0.0F;
    bool havePreviousSample = false;
};

} // namespace

struct PocketSphinxPhonemeBackend::Impl
{
#if defined(VOICE2VOCALSYNTH_WITH_POCKETSPHINX)
    ps_decoder_t* decoder = nullptr;
#endif
    bool utteranceStarted = false;
    bool haveInputEnd = false;
    double inputEndSeconds = 0.0;
    double decoderStreamStartSeconds = 0.0;
    int emittedEndFrame = -1;
    StreamingResampler resampler;

    ~Impl()
    {
#if defined(VOICE2VOCALSYNTH_WITH_POCKETSPHINX)
        if (decoder != nullptr) {
            if (utteranceStarted) {
                (void)ps_end_utt(decoder);
            }
            ps_free(decoder);
        }
#endif
    }
};

std::filesystem::path defaultPocketSphinxModelRoot()
{
#if defined(VOICE2VOCALSYNTH_POCKETSPHINX_MODEL_ROOT)
    return std::filesystem::path(VOICE2VOCALSYNTH_POCKETSPHINX_MODEL_ROOT);
#else
    return {};
#endif
}

PocketSphinxPhonemeBackend::PocketSphinxPhonemeBackend(
    PocketSphinxPhonemeBackendOptions options)
    : options_(std::move(options)),
      impl_(std::make_unique<Impl>())
{
    if (options_.modelRoot.empty()) {
        options_.modelRoot = defaultPocketSphinxModelRoot();
    }
}

PocketSphinxPhonemeBackend::~PocketSphinxPhonemeBackend() = default;
PocketSphinxPhonemeBackend::PocketSphinxPhonemeBackend(
    PocketSphinxPhonemeBackend&&) noexcept = default;
PocketSphinxPhonemeBackend& PocketSphinxPhonemeBackend::operator=(
    PocketSphinxPhonemeBackend&&) noexcept = default;

bool PocketSphinxPhonemeBackend::load(std::string& error)
{
    error.clear();
#if !defined(VOICE2VOCALSYNTH_WITH_POCKETSPHINX)
    error = "PocketSphinx support is not available in this build";
    return false;
#else
    if (loaded()) {
        return true;
    }
    const auto acousticModel = options_.modelRoot / "en-us";
    const auto allPhoneModel = options_.modelRoot / "en-us-phone.lm.bin";
    if (!std::filesystem::is_directory(acousticModel)) {
        error = "PocketSphinx acoustic model directory not found: " + acousticModel.string();
        return false;
    }
    if (!std::filesystem::is_regular_file(allPhoneModel)) {
        error = "PocketSphinx all-phone language model not found: " + allPhoneModel.string();
        return false;
    }

    ps_config_t* config = ps_config_init(nullptr);
    if (config == nullptr) {
        error = "Unable to allocate PocketSphinx configuration";
        return false;
    }
    (void)ps_config_set_str(config, "hmm", acousticModel.string().c_str());
    (void)ps_config_set_str(config, "allphone", allPhoneModel.string().c_str());
    (void)ps_config_set_bool(config, "allphone_ci", 1);
    (void)ps_config_set_int(config, "samprate", 16000);
    (void)ps_config_set_bool(config, "bestpath", 0);
    (void)ps_config_set_str(config, "loglevel", "ERROR");

    impl_->decoder = ps_init(config);
    ps_config_free(config);
    if (impl_->decoder == nullptr) {
        error = "PocketSphinx failed to initialize the US-English all-phone decoder";
        return false;
    }
    if (ps_start_utt(impl_->decoder) < 0) {
        ps_free(impl_->decoder);
        impl_->decoder = nullptr;
        error = "PocketSphinx failed to start an utterance";
        return false;
    }
    impl_->utteranceStarted = true;
    return true;
#endif
}

bool PocketSphinxPhonemeBackend::loaded() const noexcept
{
#if defined(VOICE2VOCALSYNTH_WITH_POCKETSPHINX)
    return impl_ != nullptr && impl_->decoder != nullptr;
#else
    return false;
#endif
}

void PocketSphinxPhonemeBackend::reset()
{
    if (impl_ == nullptr) {
        return;
    }
    impl_->haveInputEnd = false;
    impl_->inputEndSeconds = 0.0;
    impl_->decoderStreamStartSeconds = 0.0;
    impl_->emittedEndFrame = -1;
    impl_->resampler.reset();
#if defined(VOICE2VOCALSYNTH_WITH_POCKETSPHINX)
    if (impl_->decoder != nullptr) {
        if (impl_->utteranceStarted) {
            (void)ps_end_utt(impl_->decoder);
        }
        impl_->utteranceStarted = ps_start_utt(impl_->decoder) == 0;
    }
#endif
}

const char* PocketSphinxPhonemeBackend::name() const noexcept
{
    return "pocketsphinx_allphone";
}

PhonemeBackendDescriptor PocketSphinxPhonemeBackend::descriptor() const
{
    PhonemeBackendDescriptor descriptor;
    descriptor.backendName = name();
    descriptor.sampleRateHz = kPocketSphinxSampleRateHz;
    descriptor.windowMs = 25.0;
    descriptor.hopMs = 10.0;
    descriptor.inputKind = PhonemeBackendInputKind::MonoPcm;
    descriptor.labels = arpabetInventory();
    descriptor.confidenceMin = 0.0;
    descriptor.confidenceMax = 1.0;
    descriptor.timestampSemantics = "frame_end_seconds";
    return descriptor;
}

PhonemeBackendResult PocketSphinxPhonemeBackend::process(
    const PhonemeBackendAudioFrame& frame)
{
    PhonemeBackendResult result;
    const auto started = std::chrono::steady_clock::now();
    if (!loaded()) {
        result.ok = false;
        result.error = "PocketSphinx backend is not loaded";
        return result;
    }
    if (frame.sampleRateHz <= 0.0 || frame.monoSamples.empty()) {
        result.ok = false;
        result.error = "PocketSphinx requires non-empty mono PCM with a positive sample rate";
        return result;
    }

    const double frameDuration =
        static_cast<double>(frame.monoSamples.size()) / frame.sampleRateHz;
    const double frameEnd = frame.streamTimeStartSeconds + frameDuration;
    const bool firstChunk = !impl_->haveInputEnd;
    std::size_t skip = 0;
    if (impl_->haveInputEnd) {
        const double overlapSeconds = impl_->inputEndSeconds - frame.streamTimeStartSeconds;
        if (overlapSeconds > 0.0) {
            skip = std::min(frame.monoSamples.size(),
                            static_cast<std::size_t>(
                                std::ceil(overlapSeconds * frame.sampleRateHz - 1.0e-9)));
        }
    }
    impl_->haveInputEnd = true;
    impl_->inputEndSeconds = std::max(impl_->inputEndSeconds, frameEnd);

    if (skip < frame.monoSamples.size()) {
        if (firstChunk) {
            impl_->decoderStreamStartSeconds =
                frame.streamTimeStartSeconds + static_cast<double>(skip) / frame.sampleRateHz;
        }
        const auto pcm = impl_->resampler.process(frame.monoSamples.data() + skip,
                                                  frame.monoSamples.size() - skip,
                                                  frame.sampleRateHz);
#if defined(VOICE2VOCALSYNTH_WITH_POCKETSPHINX)
        if (!pcm.empty() &&
            ps_process_raw(impl_->decoder, pcm.data(), pcm.size(), 0, 0) < 0) {
            result.ok = false;
            result.error = "PocketSphinx failed while processing live PCM";
            return result;
        }
#endif
    }

    struct Segment
    {
        std::string phone;
        int startFrame = 0;
        int endFrame = 0;
    };
    std::vector<Segment> segments;
#if defined(VOICE2VOCALSYNTH_WITH_POCKETSPHINX)
    for (ps_seg_t* segment = ps_seg_iter(impl_->decoder);
         segment != nullptr;
         segment = ps_seg_next(segment)) {
        Segment item;
        if (const char* word = ps_seg_word(segment)) {
            item.phone = normalizePhone(word);
        }
        ps_seg_frames(segment, &item.startFrame, &item.endFrame);
        segments.push_back(std::move(item));
    }
#endif

    // The final partial segment may still be revised. Emit every newly completed
    // segment before it, preserving PocketSphinx's 10 ms frame boundaries so
    // short phones are not lost by the shell's slower UI timer.
    for (std::size_t index = 0; index + 1 < segments.size(); ++index) {
        const auto& segment = segments[index];
        if (segment.endFrame <= impl_->emittedEndFrame) {
            continue;
        }
        const int monotonicStartFrame =
            std::max(segment.startFrame, impl_->emittedEndFrame + 1);
        const double startSeconds =
            impl_->decoderStreamStartSeconds + static_cast<double>(monotonicStartFrame) / 100.0;
        const double endSeconds =
            impl_->decoderStreamStartSeconds + static_cast<double>(segment.endFrame + 1) / 100.0;
        if (!segment.phone.empty()) {
            PhonemeFrame committed;
            committed.arpabet = segment.phone;
            committed.confidence =
                std::clamp(options_.commitmentConfidence, 0.0F, 1.0F);
            committed.estimatedOnsetSeconds = startSeconds;
            committed.estimatedEndSeconds = endSeconds;
            committed.isVowel =
                PhonemeFallbackMapper::isArpabetVowel(committed.arpabet);
            committed.isConsonant = !committed.isVowel;
            committed.isVoiced = committed.isVowel;
            result.committedFrames.push_back(std::move(committed));
        }
        impl_->emittedEndFrame = segment.endFrame;
    }
    result.backendLatencyMs = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - started)
                                  .count();
    return result;
}

} // namespace Voice2VocalSynth
