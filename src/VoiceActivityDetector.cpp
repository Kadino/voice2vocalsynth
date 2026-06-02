#include "Voice2VocalSynth/VoiceActivityDetector.h"

#include <algorithm>
#include <cmath>

namespace Voice2VocalSynth
{

const char* speechBoundaryKindName(SpeechBoundaryKind kind) noexcept
{
    switch (kind) {
        case SpeechBoundaryKind::Onset:
            return "speech_onset";
        case SpeechBoundaryKind::End:
            return "speech_end";
    }
    return "speech_onset";
}

VoiceActivityDetector::VoiceActivityDetector(VoiceActivityDetectorOptions options)
    : opt_(options)
{
}

void VoiceActivityDetector::set_options(VoiceActivityDetectorOptions options)
{
    opt_ = options;
}

const VoiceActivityDetectorOptions& VoiceActivityDetector::options() const noexcept
{
    return opt_;
}

void VoiceActivityDetector::reset()
{
    pending_.clear();
    in_speech_ = false;
    onset_pending_ = false;
    hangover_active_ = false;
    onset_candidate_since_ = 0.0;
    silence_since_ = 0.0;
    last_time_ = 0.0;
    have_last_time_ = false;
}

float VoiceActivityDetector::rms_from_mono(const float* samples, int num_samples) noexcept
{
    if (samples == nullptr || num_samples <= 0) {
        return 0.0F;
    }

    double acc = 0.0;
    for (int i = 0; i < num_samples; ++i) {
        const double s = static_cast<double>(samples[i]);
        acc += s * s;
    }
    return static_cast<float>(std::sqrt(acc / static_cast<double>(num_samples)));
}

void VoiceActivityDetector::push_boundary(SpeechBoundaryKind kind,
                                          double stream_time_seconds,
                                          float rms)
{
    pending_.push_back(SpeechBoundaryEvent {kind, stream_time_seconds, rms});
}

bool VoiceActivityDetector::try_pop_boundary(SpeechBoundaryEvent& out)
{
    if (pending_.empty()) {
        return false;
    }
    out = pending_.front();
    pending_.pop_front();
    return true;
}

void VoiceActivityDetector::observe_rms(float rms, double stream_time_seconds)
{
    if (!have_last_time_) {
        last_time_ = stream_time_seconds;
        have_last_time_ = true;
    }

    last_time_ = stream_time_seconds;

    if (!in_speech_) {
        if (rms >= opt_.onset_rms_threshold) {
            if (!onset_pending_) {
                onset_pending_ = true;
                onset_candidate_since_ = stream_time_seconds;
            } else if (stream_time_seconds - onset_candidate_since_ >= opt_.min_onset_seconds) {
                in_speech_ = true;
                onset_pending_ = false;
                hangover_active_ = false;
                push_boundary(SpeechBoundaryKind::Onset, stream_time_seconds, rms);
            }
        } else {
            onset_pending_ = false;
        }
        return;
    }

    if (rms >= opt_.release_rms_threshold) {
        hangover_active_ = false;
        return;
    }

    if (!hangover_active_) {
        hangover_active_ = true;
        silence_since_ = stream_time_seconds;
    }

    if (stream_time_seconds - silence_since_ >= opt_.hangover_seconds) {
        in_speech_ = false;
        onset_pending_ = false;
        hangover_active_ = false;
        push_boundary(SpeechBoundaryKind::End, stream_time_seconds, rms);
    }
}

} // namespace Voice2VocalSynth
