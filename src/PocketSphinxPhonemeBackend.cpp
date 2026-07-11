#include "Voice2VocalSynth/PocketSphinxPhonemeBackend.h"

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

std::vector<std::int16_t> resampleTo16k(const float* samples,
                                        const std::size_t count,
                                        const double sourceRate)
{
    if (samples == nullptr || count == 0 || sourceRate <= 0.0) {
        return {};
    }

    const auto outputCount = static_cast<std::size_t>(
        std::max(1.0, std::round(static_cast<double>(count) * kPocketSphinxSampleRateHz /
                                 sourceRate)));
    std::vector<std::int16_t> output(outputCount);
    const double sourcePerOutput = sourceRate / kPocketSphinxSampleRateHz;
    for (std::size_t index = 0; index < outputCount; ++index) {
        const double sourcePosition = static_cast<double>(index) * sourcePerOutput;
        const auto lower = std::min(static_cast<std::size_t>(sourcePosition), count - 1);
        const auto upper = std::min(lower + 1, count - 1);
        const float fraction = static_cast<float>(sourcePosition - static_cast<double>(lower));
        const float value = std::clamp(samples[lower] * (1.0F - fraction) +
                                           samples[upper] * fraction,
                                       -1.0F,
                                       1.0F);
        output[index] = static_cast<std::int16_t>(std::lround(value * 32767.0F));
    }
    return output;
}

} // namespace

struct PocketSphinxPhonemeBackend::Impl
{
#if defined(VOICE2VOCALSYNTH_WITH_POCKETSPHINX)
    ps_decoder_t* decoder = nullptr;
#endif
    bool utteranceStarted = false;
    bool haveInputEnd = false;
    double inputEndSeconds = 0.0;

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
        const auto pcm = resampleTo16k(frame.monoSamples.data() + skip,
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

    std::string currentPhone;
#if defined(VOICE2VOCALSYNTH_WITH_POCKETSPHINX)
    for (ps_seg_t* segment = ps_seg_iter(impl_->decoder);
         segment != nullptr;
         segment = ps_seg_next(segment)) {
        if (const char* word = ps_seg_word(segment)) {
            currentPhone = normalizePhone(word);
        }
    }
#endif

    PhonemeTemporalObservation observation;
    observation.stream_time_seconds = frameEnd;
    observation.arpabet = std::move(currentPhone);
    observation.confidence =
        observation.arpabet.empty() ? 0.0F : std::clamp(options_.commitmentConfidence, 0.0F, 1.0F);
    result.observations.push_back(std::move(observation));
    result.backendLatencyMs = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - started)
                                  .count();
    return result;
}

} // namespace Voice2VocalSynth
