#include "Voice2VocalSynth/OfflineRenderer.h"

#include "Voice2VocalSynth/PcmWavReader.h"
#include "Voice2VocalSynth/PitchTarget.h"

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

[[nodiscard]] double effectiveSourceRecordingHz(double eventHz, double optionHz)
{
    if (std::isfinite(eventHz) && eventHz > 0.0) {
        return eventHz;
    }
    if (std::isfinite(optionHz) && optionHz > 0.0) {
        return optionHz;
    }
    return PitchTargetCalculator::midiToFrequency(60.0);
}

[[nodiscard]] double pitchShiftRatio(double targetHz, double sourceHz)
{
    if (!std::isfinite(targetHz) || targetHz <= 0.0) {
        return 1.0;
    }
    if (!std::isfinite(sourceHz) || sourceHz <= 0.0) {
        return 1.0;
    }
    const double r = targetHz / sourceHz;
    if (!std::isfinite(r) || r <= 0.0) {
        return 1.0;
    }
    return r;
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

        const double sourceHz = effectiveSourceRecordingHz(event.sourceRecordingFrequencyHz,
                                                           options.defaultSourceRecordingFrequencyHz);
        const double targetHz =
            (std::isfinite(event.targetFrequencyHz) && event.targetFrequencyHz > 0.0)
                ? event.targetFrequencyHz
                : sourceHz;
        const double pitchRatio = pitchShiftRatio(targetHz, sourceHz);

        std::size_t outEventSamples = durationSamples(event.durationMs, result.sampleRate);
        if (event.perceivedUtteranceEndSeconds > event.startTimeSeconds + 1.0e-9) {
            const double relSeconds = event.perceivedUtteranceEndSeconds - event.startTimeSeconds;
            const auto cap = static_cast<std::size_t>(
                std::max(1.0, std::ceil(relSeconds * static_cast<double>(result.sampleRate))));
            outEventSamples = std::min(outEventSamples, cap);
        }

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

        const auto emit = [&](std::size_t j, float sample) {
            const std::size_t dst = outOffset + j;
            if (dst >= result.mono.size()) {
                return;
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
        };

        const double consonantEnd =
            startSampleD + event.otoTiming.consonantMs * static_cast<double>(fileSr) / 1000.0;
        const double holdStartD =
            std::clamp(consonantEnd, startSampleD, std::max(startSampleD, endSampleD - 1.0));
        const double holdEndD = endSampleD;
        const double holdSpanSamples = std::max(0.0, holdEndD - holdStartD);
        const double tailSamples =
            std::clamp(0.12 * holdSpanSamples, 24.0, std::max(24.0, holdSpanSamples * 0.42));
        const double boundedTail = std::min(tailSamples, std::max(0.0, holdSpanSamples - 24.0));
        const double loopEndD = holdEndD - boundedTail;
        const double loopSpanSamples = std::max(0.0, loopEndD - holdStartD);
        const double consonantSpanSamples = std::max(0.0, holdStartD - startSampleD);

        std::size_t nTailOut = std::max<std::size_t>(1, msToSamples(26.0, result.sampleRate));
        nTailOut = std::min(nTailOut, std::max<std::size_t>(1u, outEventSamples / 4u));
        std::size_t nConsonantOut =
            std::min(msToSamples(event.otoTiming.consonantMs, result.sampleRate),
                     std::max<std::size_t>(1u, outEventSamples / 4u));
        if (nConsonantOut + nTailOut + 4 >= outEventSamples) {
            nConsonantOut = std::min(nConsonantOut, outEventSamples / 5u);
            nTailOut = std::min(nTailOut, outEventSamples / 5u);
        }
        const std::size_t nLoopOut = (outEventSamples > nConsonantOut + nTailOut)
            ? outEventSamples - nConsonantOut - nTailOut
            : 0;

        const bool useSustainLoop = pitchRatio > 1.0 + 1.0e-9 && boundedTail >= 8.0 && loopSpanSamples > 48.0
            && nLoopOut >= 12;

        if (useSustainLoop) {
            std::size_t jout = 0;
            const double stepC = (nConsonantOut > 0 && consonantSpanSamples > 0.0)
                ? (consonantSpanSamples / static_cast<double>(nConsonantOut)) * pitchRatio
                : 0.0;
            for (; jout < nConsonantOut; ++jout) {
                const double srcIndex = startSampleD + static_cast<double>(jout) * stepC;
                emit(jout, sampleLinear(src, srcIndex));
            }

            const double Lloop = std::max(1.0, loopSpanSamples);
            const double stepL = (loopSpanSamples > 0.0 && nLoopOut > 0)
                ? (loopSpanSamples / static_cast<double>(nLoopOut)) * pitchRatio
                : 0.0;
            double acc = 0.0;
            for (std::size_t k = 0; k < nLoopOut; ++k, ++jout) {
                double wrapped = std::fmod(acc, Lloop);
                if (wrapped < 0.0) {
                    wrapped += Lloop;
                }
                const double srcIndex = holdStartD + wrapped;
                emit(jout, sampleLinear(src, srcIndex));
                acc += stepL;
            }

            const double tailSrcLen = std::max(1.0, holdEndD - loopEndD);
            const double stepT =
                (nTailOut > 0) ? (tailSrcLen / static_cast<double>(nTailOut)) * pitchRatio : 0.0;
            for (std::size_t k = 0; k < nTailOut; ++k, ++jout) {
                const double srcIndex = loopEndD + static_cast<double>(k) * stepT;
                emit(jout, sampleLinear(src, srcIndex));
            }
            for (; jout < outEventSamples; ++jout) {
                const double tailEnd = std::min(holdEndD - 1.0, loopEndD + tailSrcLen - 1.0);
                emit(jout, sampleLinear(src, tailEnd));
            }
        } else {
            const double srcStep = (regionLen > 1.0e-6 && outEventSamples > 0)
                ? (regionLen / static_cast<double>(outEventSamples)) * pitchRatio
                : 0.0;

            if (srcStep > 0.0 && outEventSamples > 0) {
                const double lastSrc = startSampleD + srcStep * static_cast<double>(outEventSamples - 1u);
                if (lastSrc > endSampleD - 1.0e-6) {
                    std::ostringstream w;
                    w << "pitch shift for alias=\"" << event.alias
                      << "\" reads past oto region end (truncated tail)";
                    result.warnings.push_back(w.str());
                }
            }

            for (std::size_t j = 0; j < outEventSamples; ++j) {
                float sample = 0.0f;
                if (srcStep > 0.0) {
                    const double srcIndex = startSampleD + static_cast<double>(j) * srcStep;
                    sample = sampleLinear(src, srcIndex);
                }
                emit(j, sample);
            }
        }
    }

    result.ok = true;
    return result;
}

} // namespace Voice2VocalSynth
