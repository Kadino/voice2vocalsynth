# Live pipeline roadmap (agent / future sessions)

This document captures the **gap analysis and prioritized plan** agreed for Voice2VocalSynth so future work sessions can continue without re-deriving scope from chat history. The canonical product spec remains [`voice2vocalsynth.spec.md`](../voice2vocalsynth.spec.md).

## Implementation status legend

- [x] **Milestone 1:** minimal live capture → analysis hooks → logging (JUCE shell).
- [x] **Milestone 2 (v1):** temporal stabilizer in core + `PhonemeFrame` commits + shell JSON log; dedicated CSV writer deferred.
- [x] **Milestone 3 (v1):** VAD boundaries + inference lag tracker + playback-aligned sustain release.
- [x] **Milestone 5 (v1):** whistle detector v0 (Goertzel HNR-style, separate from phoneme ONNX).
- [ ] …


## Milestone 1 — Minimal live closed loop (highest leverage)

**Goal:** Prove capture/analysis timelines and async inference plumbing before full synthesis.

- [x] Audio input in the JUCE shell (existing pass-through) extended with a **mono capture buffer** and **stream sample clock**.
- [x] **Pitch:** block-wise **autocorrelation pitch estimate** → `RecentPitchTracker` / `PitchTargetCalculator` path used elsewhere.
- [x] **Phoneme (stub):** when ONNX is enabled in the build, periodically enqueue frames into **`PhonemeOnnxAsyncRunner`** (dummy identity model: fixed-size float tensor); drain completions on the UI timer.
- [x] **Log:** append compact **JSON lines** (pitch + ONNX job id + stream time + **steady_clock** completion time as `steady_ns`) to an in-shell read-only log for inspection.

## Milestone 2 — Temporal phoneme stabilizer

- [x] State machine on top of streaming hypotheses: **min segment duration**, **candidate-stable switch** with **confidence hysteresis**, **silence dwell** before closing a segment (`phonemeDetection.temporalStabilizer` v1).
- [x] Emit **`PhonemeFrame`**-shaped committed segments (`try_pop_committed`); JUCE shell logs them as **`ph_frame`** JSON. **Hybrid for testing (keep):** stabilizer input stays a **pitch-gated placeholder** (voiced pitch → `AH`) in parallel with the **ONNX stub** jobs (`onnx` JSON lines), so boundaries and async plumbing can be debugged before a real phoneme classifier exists.
- [ ] Optional: write the same records to session **CSV** alongside future `phonemes.csv` (`dataStorage.recordingDebugFormat`).

## Milestone 3 — VAD + boundary timestamps

- [x] `speech_onset` / `speech_end` with stream timestamps (`vadSynchronization.boundaryRepresentation`).
- [x] Bounded moving estimate of inference+queue lag (`vadSynchronization.latencyAlignment.inferenceJitter`).
- [x] Renderer policy: sustain release aligned to **perceived** utterance end (`vadSynchronization.rendererInteraction`).

## Milestone 4 — Config-driven ARPABET mapping

- [ ] Load `phonemeToJapaneseMapping` from user file (`phonemeToJapaneseMapping.mustLiveInConfigFile`); keep code defaults only as fallback.

## Milestone 5 — Whistle detector v0

- [x] Lightweight spectral / HNR-style flag; separate path from phoneme ONNX (`whistleDetection`).

## Milestone 6 — Measured latency

- [ ] Optional loopback / correlation to augment **estimated** breakdown (`latencyDesign.requirement`).

## Milestone 7 — Real phoneme ONNX (parallel track)

- [ ] Train or integrate a streaming ARPABET-classifier ONNX matching stabilizer input/output contracts.

## Reference: major spec gaps (summary)

| Area | Spec | Repo (pre–live-MVP) |
|------|------|---------------------|
| Full `signalPipeline` live | Yes | Mostly offline / libraries |
| VAD + render-aligned release | Yes | `VoiceActivityDetector` + `UtteranceSustainReleasePolicy` + offline `perceivedUtteranceEndSeconds` |
| `PhonemeFrame` streaming | Yes | Stabilizer + shell `ph_frame` JSON (phoneme ONNX head TBD) |
| Whistle detector | Separate detector | `WhistleDetector` + live `whistle` / `whistle_edge` JSON |
| Mapping in config file | Required | Code defaults |
| WORLD-quality renderer | Planned | Naive resample offline |

Update the milestone checkboxes as the shell and core evolve.
