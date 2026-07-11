#pragma once

#include "Voice2VocalSynth/PhonemeBackend.h"

#include <filesystem>
#include <memory>

namespace Voice2VocalSynth
{

struct PocketSphinxPhonemeBackendOptions
{
    /// Directory containing `en-us/` and `en-us-phone.lm.bin`.
    std::filesystem::path modelRoot;
    /// PocketSphinx does not expose posteriors for partial all-phone results.
    /// This is a commitment/stability confidence, not an acoustic posterior.
    float commitmentConfidence = 0.80F;
};

/// Incremental 16 kHz US-English all-phone recognizer.
///
/// The bundled PocketSphinx model emits unstressed CMU ARPABET directly. Input
/// frames may overlap; the backend consumes only the unseen suffix and
/// resamples it to 16 kHz before advancing the decoder.
class PocketSphinxPhonemeBackend final : public IPhonemeBackend
{
public:
    explicit PocketSphinxPhonemeBackend(PocketSphinxPhonemeBackendOptions options = {});
    ~PocketSphinxPhonemeBackend() override;

    PocketSphinxPhonemeBackend(const PocketSphinxPhonemeBackend&) = delete;
    PocketSphinxPhonemeBackend& operator=(const PocketSphinxPhonemeBackend&) = delete;
    PocketSphinxPhonemeBackend(PocketSphinxPhonemeBackend&&) noexcept;
    PocketSphinxPhonemeBackend& operator=(PocketSphinxPhonemeBackend&&) noexcept;

    [[nodiscard]] bool load(std::string& error);
    [[nodiscard]] bool loaded() const noexcept;
    void reset() override;

    [[nodiscard]] const char* name() const noexcept override;
    [[nodiscard]] PhonemeBackendDescriptor descriptor() const override;
    [[nodiscard]] PhonemeBackendResult process(const PhonemeBackendAudioFrame& frame) override;

private:
    PocketSphinxPhonemeBackendOptions options_;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::filesystem::path defaultPocketSphinxModelRoot();

} // namespace Voice2VocalSynth
