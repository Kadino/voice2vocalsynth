#include "Voice2VocalSynth/OfflineRenderer.h"

#include "Voice2VocalSynth/PcmWavReader.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace Voice2VocalSynth
{
namespace
{

[[nodiscard]] float sampleLinear(const std::vector<float>& wav, double index)
{
    if (wav.empty()) {
        return 0.0f;
    }
    if (index <= 0.0) {
        return wav.front();
    }
    if (index >= static_cast<double>(wav.size() - 1)) {
        return wav.back();
    }
    const auto i0 = static_cast<std::size_t>(index);
    const float frac = static_cast<float>(index - static_cast<double>(i0));
    return wav[i0] * (1.0f - frac) + wav[i0 + 1u] * frac;
}

[[nodiscard]] std::size_t msToSamples(double ms, int sampleRate)
{
    if (ms <= 0.0 || sampleRate <= 0) {
        return 0;
    }
    const double s = std::round(ms * static_cast<double>(sampleRate) / 1000.0);
    return static_cast<std::size_t>(std::max(0.0, s));
}

[[nodiscard]] std::size_t durationSamples(double durationMs, int sampleRate)
{
    if (durationMs <= 0.0 || sampleRate <= 0) {
        return 0;
    }
    const double s = std::ceil(durationMs * static_cast<double>(sampleRate) / 1000.0);
    return static_cast<std::size_t>(std::max(1.0, s));
}

} // namespace

OfflineRenderResult OfflineRenderer::render(const RenderPlan& plan, const OfflineRenderOptions& options)
{
    OfflineRenderResult result;
    if (options.outputSampleRate <= 0) {
        result.error = "outputSampleRate must be positive";
        return result;
    }
    result.sampleRate = options.outputSampleRate;

    if (plan.events.empty()) {
        result.ok = true;
        result.warnings.push_back("render plan has no events; output is empty");
        return result;
    }

    double timelineEndSeconds = 0.0;
    for (const auto& ev : plan.events) {
        timelineEndSeconds =
            std::max(timelineEndSeconds, ev.startTimeSeconds + ev.durationMs / 1000.0);
    }

    const std::size_t outLength =
        static_cast<std::size_t>(std::ceil(timelineEndSeconds * static_cast<double>(result.sampleRate)));
    result.mono.assign(outLength, 0.0f);

    for (std::size_t eventIndex = 0; eventIndex < plan.events.size(); ++eventIndex) {
        const auto& event = plan.events[eventIndex];
        const auto wavPath = options.voicebankRoot / event.wavFile;
        const auto loaded = PcmWavReader::loadMonoFloat(wavPath);
        if (!loaded.ok) {
            std::ostringstream w;
            w << "skip event alias=\"" << event.alias << "\" wav=\"" << event.wavFile
              << "\": " << loaded.error;
            result.warnings.push_back(w.str());
            continue;
        }

        const int fileSr = loaded.sampleRate;
        const auto& src = loaded.mono;
        if (src.empty()) {
            result.warnings.push_back("empty WAV for alias=\"" + event.alias + "\"");
            continue;
        }

        const double rightTrimMs = std::abs(event.otoTiming.cutoffMs);
        const std::size_t rightTrimSamples = msToSamples(rightTrimMs, fileSr);
        const double startSampleD = event.otoTiming.offsetMs * static_cast<double>(fileSr) / 1000.0;
        const double endSampleD =
            static_cast<double>(src.size()) - static_cast<double>(rightTrimSamples);
        const double regionLen = std::max(0.0, endSampleD - startSampleD);

        const std::size_t outEventSamples = durationSamples(event.durationMs, result.sampleRate);
        std::size_t outOffset = static_cast<std::size_t>(
            std::max(0.0, std::floor(event.startTimeSeconds * static_cast<double>(result.sampleRate))));

        std::size_t overlapOutSamples = 0;
        if (eventIndex > 0 && event.otoTiming.overlapMs > 0.0) {
            overlapOutSamples = msToSamples(event.otoTiming.overlapMs, result.sampleRate);
            overlapOutSamples = std::min(overlapOutSamples, outEventSamples);
            if (overlapOutSamples > 0) {
                if (outOffset >= overlapOutSamples) {
                    outOffset -= overlapOutSamples;
                } else {
                    overlapOutSamples = outOffset;
                    outOffset = 0;
                }
            }
        }

        for (std::size_t j = 0; j < outEventSamples; ++j) {
            const std::size_t dst = outOffset + j;
            if (dst >= result.mono.size()) {
                break;
            }

            float sample = 0.0f;
            if (regionLen > 1.0e-6) {
                const double srcIndex =
                    startSampleD + (static_cast<double>(j) * regionLen) / static_cast<double>(outEventSamples);
                sample = sampleLinear(src, srcIndex);
            }

            if (overlapOutSamples > 0 && j < overlapOutSamples) {
                const float existing = result.mono[dst];
                float alpha = 1.0f;
                if (overlapOutSamples == 1) {
                    alpha = 1.0f;
                } else {
                    alpha = static_cast<float>(j) / static_cast<float>(overlapOutSamples - 1);
                }
                result.mono[dst] = existing * (1.0f - alpha) + sample * alpha;
            } else {
                result.mono[dst] += sample;
            }
        }
    }

    result.ok = true;
    return result;
}

} // namespace Voice2VocalSynth
