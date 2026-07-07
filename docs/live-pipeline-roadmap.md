# Live pipeline roadmap (agent / future sessions)

This document captures the **gap analysis and prioritized plan** agreed for Voice2VocalSynth so future work sessions can continue without re-deriving scope from chat history. The canonical product spec remains [`voice2vocalsynth.spec.md`](../voice2vocalsynth.spec.md). For the Linux-first, near real time development verification plan for phoneme detection, see [`live-phoneme-verification-plan.md`](live-phoneme-verification-plan.md).

## Implementation status legend

- [x] **Milestone 1:** minimal live capture → analysis hooks → logging (JUCE shell).
- [x] **Milestone 2 (v1):** temporal stabilizer in core + `PhonemeFrame` commits + shell JSON log; dedicated CSV writer deferred.
- [x] **Milestone 3 (v1):** VAD boundaries + inference lag tracker + playback-aligned sustain release policy.
- [x] **Milestone 4 (v1):** config-driven ARPABET → Japanese mapping JSON.
- [x] **Milestone 5 (v1):** whistle detector v0.
- [x] **Milestone 6 (v1):** measured loopback latency (correlation probe + augment estimate / playback mapping).


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

- [x] Load `phonemeToJapaneseMapping` from user file (`phonemeToJapaneseMapping.mustLiveInConfigFile`); keep code defaults only as fallback.

## Milestone 5 — Whistle detector v0

- [x] Lightweight spectral / HNR-style flag; separate path from phoneme ONNX (`whistleDetection`).

## Milestone 6 — Measured latency

- [x] `LoopbackLatencyMeasurer`: inject MLS-like probe on output, normalized cross-correlation on input (loopback required).
- [x] `MeasuredLatencySummary` + JUCE shell **Measure loopback latency** UI; `latency_measure` JSON log line.
- [x] Playback boundary mapping prefers measured end-to-end when valid (`PlaybackBoundaryMapper` optional override).

## Milestone 7 — Real phoneme ONNX (parallel track)

- [x] Define a swappable phoneme backend interface and placeholder backend so the
      current pitch-gated `AH` path can be replaced without changing stabilizer
      consumers.
- [x] Add initial phoneme-frame evaluation metrics for comparing candidate
      backends against labeled fixtures.
- [x] Add file-based JSON label/prediction loading and metrics JSON export for
      backend bakeoffs.
- [ ] Train or integrate a streaming ARPABET-classifier ONNX matching stabilizer input/output contracts.

## Next execution plan — backend bakeoff → live synthesis

This section is the current handoff plan for future agents/programmers. Execute
items in order unless a later task is explicitly needed to unblock validation.

### 1. Add a phoneme evaluation CLI

**Goal:** Make backend comparisons runnable outside unit tests.

- [x] Add a small executable target, e.g. `Voice2VocalSynthPhonemeEval`.
- Inputs:
  - `--reference <reference_frames.json>`
  - `--prediction <predicted_frames.json>`
  - optional `--out <metrics.json>`
  - optional thresholds mirroring `PhonemeEvaluationOptions`
    (`--max-onset-error-ms`, `--min-overlap-ms`).
- Use existing core APIs:
  - `loadPhonemeFrameLabelsJson`
  - `evaluatePhonemeFrames`
  - `phonemeEvaluationMetricsToJson`
- Expected output:
  - stdout summary for quick inspection,
  - JSON metrics file when `--out` is provided.
- Validation:
  - Unit test the argument-independent evaluator code.
  - Add a fixture pair under `tests/fixtures/phoneme_eval/` if it contains no
    private recorded audio.

### 2. Define the real streaming phoneme backend adapter contract

**Goal:** Remove ambiguity before integrating candidate models.

- [x] Extend or document `IPhonemeBackend` with model-facing assumptions:
  - expected sample rate,
  - frame/window length,
  - hop size,
  - whether samples or features are passed,
  - output phoneme inventory,
  - timestamp semantics,
  - confidence normalization range.
- Keep `PlaceholderPitchPhonemeBackend` as an explicit debug backend.
- Decide whether the first real ONNX adapter consumes:
  - raw mono PCM windows, or
  - precomputed features such as log-mel frames.
- Acceptance criteria:
  - another backend can be added without editing `PhonemeTemporalStabilizer`;
  - output is `PhonemeTemporalObservation` or directly convertible to it.

### 3. Add an ONNX phoneme backend adapter

**Goal:** Convert ONNX Runtime tensor output into phoneme observations.

- [x] Keep `PhonemeOnnxRunner` as low-level model execution.
- [x] Add an adapter above it, e.g. `PhonemeOnnxBackend`.
- Responsibilities:
  - load model metadata/config,
  - prepare input tensor/features,
  - decode output logits/probabilities into ARPABET labels,
  - attach stream timestamps and confidence,
  - return `PhonemeBackendResult`.
- Do not assume all ONNX models have the same output layout; use a sidecar JSON
  model config if needed:

```json
{
  "sampleRateHz": 16000,
  "windowMs": 160,
  "hopMs": 20,
  "inputKind": "monoPcm",
  "labels": ["sil", "AA", "AE", "AH"]
}
```

### 4. Build private evaluation data outside the repo

**Goal:** Compare backends on the user's actual target sounds without committing
private voice data.

- [x] Store recordings and labels outside Git, for example:
  - `%LOCALAPPDATA%/Voice2VocalSynth/EvalData` on Windows,
  - a user-selected private data folder from app settings.
- Do **not** commit WAV recordings or private labels.
- Suggested first dataset:
  - spoken vowels,
  - sung vowels,
  - whispered vowels,
  - plosives: `P T K B D G`,
  - fricatives: `S SH F TH V Z`,
  - nasals: `M N NG`,
  - `R/L/W/Y`,
  - nonsense syllables,
  - whistle examples.
- Label format should match the JSON accepted by
  `loadPhonemeFrameLabelsJson`:

```json
[
  { "arpabet": "K", "start": 0.120, "end": 0.165, "confidence": 1.0 },
  { "arpabet": "AE", "start": 0.165, "end": 0.410, "confidence": 1.0 },
  { "arpabet": "T", "start": 0.410, "end": 0.455, "confidence": 1.0 }
]
```

### 5. Run backend bakeoffs

**Goal:** Determine if another backend performs better using objective metrics.

- [x] Compare at minimum:
  - `PlaceholderPitchPhonemeBackend` baseline,
  - first real ONNX phoneme backend,
  - any alternative candidate model/backend.
- Score both:
  - raw backend observations converted to frames, and
  - post-`PhonemeTemporalStabilizer` committed frames.
- Required metrics:
  - precision,
  - recall,
  - F1,
  - missed consonant count/rate,
  - false-positive count/rate,
  - mean absolute onset error,
  - P95 onset error (add this metric if not present yet),
  - backend latency and P95 latency.
- Backend is "better" only if it improves phoneme/timing metrics without
  exceeding the latency mode being tested.

### 6. Promote backend selection into the JUCE shell

**Goal:** Make live testing switchable without recompiling.

- [x] Add live backend mode choices:
  - placeholder/debug,
  - ONNX phoneme backend,
  - optionally recorded-fixture playback.
- Persist choice in `shell_settings.json`.
- Live log should identify backend name in `ph_frame` or adjacent diagnostic
  JSON lines.
- Keep the ONNX identity/stub path only as a plumbing smoke test; do not treat it
  as phoneme detection.

### 7. Close the live synthesis loop

**Goal:** Move from live analysis logging to live synthesized output.

- [x] Pipeline target:
  - microphone chunk,
  - pitch/VAD/whistle/phoneme backend,
  - temporal stabilizer,
  - voicebank mapping plan,
  - render plan,
  - streaming render buffer,
  - JUCE output.
- Initial version may use the existing naive/offline renderer logic adapted to a
  buffered streaming scheduler.
- Keep debug timeline export enabled so every live test can inspect:
  - detected phonemes,
  - selected aliases,
  - skipped aliases,
  - pitch target,
  - latency estimate,
  - render timing.

### Pre-manual-testing polish (completed)

These items harden the live shell and bakeoff tooling before Windows audio validation:

- [x] Wire `WhistleDetector` in the live shell (`whistle` / `whistle_edge` JSON, `is_active()` bypasses phoneme placeholder).
- [x] Export `live_timeline` debug JSON from `StreamingLiveRenderer::lastTimeline()`.
- [x] Forward `sustain_release` playback time into live renderer truncation (`onSustainRelease` / `onUtteranceStart`).
- [x] Shell ONNX phoneme model paths in `shell_settings.json` (`phonemeOnnxModelPath`, `phonemeOnnxConfigPath`, `phonemeOnnxUseRepositoryFixture`, `phonemeMappingPath`).
- [x] Batch bakeoff CLI: `--all-clips` with `listEvalClipNames()` over private eval data layout.

### 8. Windows validation and Wine guidance

**Goal:** Avoid false confidence from Linux-only testing.

- [x] Linux JUCE compile smoke tests in CI (`.github/workflows/ci.yml`: core `ctest` + `Voice2VocalSynthApp` build).
- Wine can be used only as a rough launch/UI smoke test for a Windows binary.
- Wine is **not** sufficient for validating:
  - WASAPI/ASIO behavior,
  - Windows device enumeration,
  - virtual audio cable routing,
  - project-owned virtual microphone behavior,
  - real latency measurements.
- Required before relying on live audio behavior:
  - build on Windows,
  - run with the target microphone,
  - run with monitor output,
  - run with an existing virtual audio device such as VB-CABLE/VoiceMeeter,
  - capture latency and debug logs from that environment.

## Reference: major spec gaps (summary)

| Area | Spec | Repo (pre–live-MVP) |
|------|------|---------------------|
| Full `signalPipeline` live | Yes | Mostly offline / libraries |
| VAD + render-aligned release | Yes | `VoiceActivityDetector` + `UtteranceSustainReleasePolicy` + offline `perceivedUtteranceEndSeconds` |
| `PhonemeFrame` streaming | Yes | Stabilizer + shell `ph_frame` JSON (phoneme ONNX head TBD) |
| Whistle detector | Separate detector | `WhistleDetector` + live `whistle` / `whistle_edge` JSON |
| Mapping in config file | Required | `config/phoneme_to_japanese.json` + loader |
| Measured latency (loopback) | Required | `LoopbackLatencyMeasurer` + shell measure button |
| WORLD-quality renderer | Planned | Naive resample offline |

Update the milestone checkboxes as the shell and core evolve.
