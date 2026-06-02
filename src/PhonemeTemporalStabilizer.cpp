#include "Voice2VocalSynth/PhonemeTemporalStabilizer.h"
#include "Voice2VocalSynth/PhonemeFallbackMapper.h"

#include <algorithm>
#include <string>
#include <unordered_set>

namespace Voice2VocalSynth
{
namespace
{

[[nodiscard]] std::string trim_copy(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

[[nodiscard]] bool is_unvoiced_consonant_token(const std::string& upper_ascii)
{
    static const std::unordered_set<std::string> unvoiced {
        "P", "T", "K", "CH", "F", "TH", "S", "SH", "HH"};
    return unvoiced.find(upper_ascii) != unvoiced.end();
}

[[nodiscard]] std::string to_upper_ascii_token(std::string value)
{
    for (auto& ch : value) {
        if (ch >= 'a' && ch <= 'z') {
            ch = static_cast<char>(ch - 'a' + 'A');
        }
    }
    return value;
}

} // namespace

PhonemeTemporalStabilizer::PhonemeTemporalStabilizer(PhonemeTemporalStabilizerOptions options)
    : opt_(std::move(options))
{
}

void PhonemeTemporalStabilizer::set_options(PhonemeTemporalStabilizerOptions options)
{
    opt_ = std::move(options);
}

const PhonemeTemporalStabilizerOptions& PhonemeTemporalStabilizer::options() const noexcept
{
    return opt_;
}

void PhonemeTemporalStabilizer::reset()
{
    committed_.clear();
    active_label_.clear();
    active_onset_ = 0.0;
    active_same_label_last_time_ = 0.0;
    active_peak_confidence_ = 0.0F;
    candidate_label_.clear();
    candidate_since_ = 0.0;
    candidate_peak_confidence_ = 0.0F;
    in_silence_run_ = false;
    silence_since_ = 0.0;
    have_last_observation_time_ = false;
    last_observation_time_ = 0.0;
}

PhonemeFrame PhonemeTemporalStabilizer::make_frame(const std::string& normalized_arpabet,
                                                   const float peak_confidence,
                                                   const double onset_seconds,
                                                   const double end_seconds)
{
    PhonemeFrame frame;
    frame.arpabet = normalized_arpabet;
    frame.confidence = peak_confidence;
    frame.estimatedOnsetSeconds = onset_seconds;
    frame.estimatedEndSeconds = end_seconds;
    const bool vowel = PhonemeFallbackMapper::isArpabetVowel(normalized_arpabet);
    frame.isVowel = vowel;
    frame.isConsonant = !vowel;
    frame.isVoiced = vowel || !is_unvoiced_consonant_token(normalized_arpabet);
    return frame;
}

void PhonemeTemporalStabilizer::flush_committed()
{
    if (active_label_.empty()) {
        return;
    }
    const double end = active_same_label_last_time_;
    const double duration = end - active_onset_;
    if (duration >= opt_.min_segment_seconds) {
        committed_.push_back(
            make_frame(active_label_, active_peak_confidence_, active_onset_, end));
    }
    active_label_.clear();
    active_peak_confidence_ = 0.0F;
    candidate_label_.clear();
    in_silence_run_ = false;
}

void PhonemeTemporalStabilizer::observe(const PhonemeTemporalObservation& observation)
{
    const double t = observation.stream_time_seconds;
    const std::string trimmed = trim_copy(observation.arpabet);
    const std::string normalized =
        trimmed.empty() ? std::string() : PhonemeFallbackMapper::normalizeArpabet(trimmed);
    const std::string upper_token = to_upper_ascii_token(normalized);

    const bool voiced = !normalized.empty() && observation.confidence >= opt_.min_observation_confidence;

    if (have_last_observation_time_ && t + 1.0e-9 < last_observation_time_) {
        reset();
    }
    last_observation_time_ = t;
    have_last_observation_time_ = true;

    if (!voiced) {
        if (!in_silence_run_) {
            in_silence_run_ = true;
            silence_since_ = t;
        }
        if (!active_label_.empty() && (t - silence_since_) >= opt_.silence_finalize_seconds) {
            flush_committed();
        }
        return;
    }

    in_silence_run_ = false;

    if (active_label_.empty()) {
        active_label_ = upper_token;
        active_onset_ = t;
        active_same_label_last_time_ = t;
        active_peak_confidence_ = observation.confidence;
        candidate_label_.clear();
        return;
    }

    if (upper_token == active_label_) {
        active_same_label_last_time_ = t;
        active_peak_confidence_ = std::max(active_peak_confidence_, observation.confidence);
        candidate_label_.clear();
        return;
    }

    const float switch_floor =
        opt_.min_observation_confidence + opt_.hysteresis_confidence_delta;
    if (observation.confidence < switch_floor) {
        return;
    }

    if (candidate_label_ != upper_token) {
        candidate_label_ = upper_token;
        candidate_since_ = t;
        candidate_peak_confidence_ = observation.confidence;
        return;
    }

    candidate_peak_confidence_ = std::max(candidate_peak_confidence_, observation.confidence);

    if ((t - candidate_since_) >= opt_.candidate_stable_seconds
        && candidate_peak_confidence_ >= switch_floor) {
        flush_committed();
        active_label_ = upper_token;
        active_onset_ = candidate_since_;
        active_same_label_last_time_ = t;
        active_peak_confidence_ = candidate_peak_confidence_;
        candidate_label_.clear();
    }
}

bool PhonemeTemporalStabilizer::try_pop_committed(PhonemeFrame& out)
{
    if (committed_.empty()) {
        return false;
    }
    out = committed_.front();
    committed_.pop_front();
    return true;
}

} // namespace Voice2VocalSynth
