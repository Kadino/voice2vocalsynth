# Live phoneme verification plan

This document is the handoff plan for verifying phoneme detection during
development. It is intentionally scoped to near real time live inference: the
verification result must come from audio routed through the live JUCE shell, not
from offline WAV-to-backend processing.

## Fixed decisions

- Dataset: LibriSpeech `test-clean`.
- Reference labels: automatic transcript-to-phoneme and forced-alignment output.
- First platform: Linux.
- Linux test target: the existing JUCE shell.
- Command style: local development command/script, not CI.
- Latency gate: end-to-end latency must be no greater than **1000 ms**.
- Backend scope: include selection and comparison of additional phoneme
  backends/models beyond the current placeholder and first ONNX path.
- Later platform pass: repeat the same verification on Windows.

Faster latency is preferred, but temporal correctness is the primary quality
requirement because detected phoneme timing drives vocal-synth re-rendering.

## Verification boundary

The formal verification run must exercise the live path:

1. LibriSpeech audio plays at 1.0x real-time speed.
2. Linux virtual audio routing presents that playback as the JUCE shell input.
3. The JUCE shell runs the selected live phoneme backend.
4. The temporal stabilizer emits `ph_frame` JSON records.
5. Post-run scoring consumes only those live log records.

Offline steps are allowed only for setup and analysis:

- downloading LibriSpeech `test-clean`;
- converting transcripts to phoneme labels;
- forced alignment and reference JSON generation;
- scoring captured live logs after the run;
- early exploratory backend bakeoffs that are not reported as live verification.

Do not use these as verification results:

- `Voice2VocalSynthPhonemeBakeoff` predictions;
- direct WAV-to-backend inference;
- faster-than-real-time playback;
- placeholder/debug backend performance as a pass result;
- the repository dummy identity ONNX fixture as phoneme detection.

## Local command target

Add a local command such as:

```sh
scripts/run_live_phoneme_verify_linux.sh
```

The command should fail with a non-zero exit code when setup, live capture,
quality gates, or latency gates fail. It should not require CI-specific audio
devices.

Responsibilities:

1. Locate or validate the local LibriSpeech `test-clean` root.
2. Locate or generate automatic phoneme reference labels.
3. Validate Linux virtual audio routing, for example ALSA loopback, JACK, or
   PipeWire/Pulse loopback.
4. Launch the existing JUCE shell with the selected live backend configuration.
5. Play selected clips at 1.0x speed into the virtual input.
6. Capture live JSONL logs to a run directory outside Git.
7. Convert `ph_frame` records into prediction frames.
8. Score predictions against reference labels.
9. Emit a report and return non-zero on failed gates.

Suggested local data layout:

```text
~/.local/share/Voice2VocalSynth/LivePhonemeVerify/
  datasets/
    LibriSpeech/test-clean/
  labels/
    librispeech-test-clean/
  runs/
    <timestamp>/
      manifest.json
      live-log.jsonl
      predictions.json
      metrics.json
      report.md
```

## Label generation

Use LibriSpeech transcripts as the source text. The automatic reference pipeline
should:

1. Normalize transcript text consistently with the G2P/alignment tool.
2. Convert words to ARPABET phonemes.
3. Force-align phoneme timing to the clip audio.
4. Export labels in the existing editable frame format accepted by
   `loadPhonemeFrameLabelsJson`, for example:

```json
[
  { "arpabet": "K", "start": 0.120, "end": 0.165, "confidence": 1.0 },
  { "arpabet": "AE", "start": 0.165, "end": 0.410, "confidence": 1.0 }
]
```

Record the label tool, version, dictionary/G2P source, and alignment parameters
in the run manifest so later agents can reproduce or replace the reference
pipeline.

## Backend selection

The development workflow should compare candidate backends before promoting one
to the live verification gate.

Include at minimum:

- `placeholder` as a negative/debug baseline only;
- the current `onnx_phoneme` live backend;
- additional candidate streaming phoneme backends/models selected during
  development.

The first additional live candidate is PocketSphinx 5.1.1 with its bundled
US-English all-phone model. CMake fetches the versioned BSD-licensed source and
model archive with a pinned SHA-256. It is a true incremental decoder and emits
the 39-phone unstressed CMU ARPABET inventory directly. PocketSphinx does not
provide acoustic posteriors for partial hypotheses, so its backend reports a
documented commitment confidence rather than presenting that value as a model
posterior. It is a baseline candidate, not a presumed quality winner.

No permissively licensed, pretrained, true-streaming English phoneme ONNX model
was available when this candidate was selected. Bidirectional Wav2Vec2/WavLM
CTC models can still be compared as windowed ONNX candidates, but they must not
be described as true streaming.

Selection criteria:

- streaming-compatible input contract;
- ARPABET output or deterministic mapping to ARPABET;
- stable timestamps on the capture stream clock;
- confidence values normalized for the stabilizer;
- practical CPU latency on the Linux development machine;
- improved temporal correctness versus existing candidates.

Offline bakeoffs may rank candidates cheaply, but every candidate that is
claimed as passing must be re-run through the live JUCE shell.

## Required live log evidence

The scorer needs enough timing evidence to prove the 1000 ms end-to-end gate and
to diagnose temporal drift. Capture these records where available:

- `ph_frame`: backend name, ARPABET label, confidence, `t0`, `t1`;
- `onnx`: stream time, `steady_ns`, `lag_ms`, `lag_est_ms`, success flag;
- `vad`: event kind, stream time, playback-mapped time;
- `latency_measure`: loopback round-trip and estimate when available;
- device settings: sample rate, buffer sizes, input/output latency samples;
- backend descriptor: sample rate, window, hop, label inventory.

If `ph_frame` records do not include a monotonic emission timestamp, add one
before treating decision latency as proven. The existing `onnx` log already
contains `steady_ns`; `ph_frame` should have equivalent timing evidence.

## Metrics and gates

The report must include phoneme quality and timing metrics:

- precision, recall, F1;
- false-positive count/rate;
- missed count/rate;
- missed consonant count/rate;
- mean absolute onset error;
- P95 onset error;
- mean/P95 end-time error;
- segment duration error;
- per-phoneme confusion summary for common failures.

The report must include latency metrics:

- measured or estimated end-to-end latency;
- phoneme decision latency when `ph_frame` emission timestamps are available;
- backend queue/inference latency from live `onnx.lag_ms`;
- P50/P95/P99/max latency where enough samples exist.

Hard gate:

- end-to-end latency must be <= 1000 ms.

Temporal correctness gate:

- set explicit onset/end/duration thresholds in the local command once the
  first automatic-label run establishes realistic label noise. Do not pass a
  backend solely because it is below the latency limit.

The Linux command currently supplies deliberately loose, provisional first-run
thresholds through environment-overridable values. Reports preserve the raw
metrics so these thresholds can be tightened after reviewing MFA label noise.
The scorer fails closed if any temporal threshold is omitted.

## Linux execution notes

Linux is the first development verification platform. Use the existing JUCE
shell rather than a separate offline harness.

Acceptable audio routing options:

- ALSA `snd-aloop`;
- JACK;
- PipeWire/Pulse virtual source/sink routing.

The command should print the selected route and fail early if it cannot observe
audio at the JUCE shell input. Because virtual audio setup is host-specific, this
is a local command rather than a CI job.

## Windows re-verification

After Linux verification is passing, repeat the same dataset, labels, backend
configuration, scoring code, and latency gate on Windows.

Windows-specific routing may use:

- WASAPI or ASIO target devices;
- VB-CABLE;
- VoiceMeeter;
- Virtual Audio Cable.

Keep Windows reports separate from Linux reports because driver latency, buffer
behavior, and device enumeration are part of the platform validation surface.

## Implementation checklist

- [x] Add persistent JSONL export for live shell logs if the UI-only log is
      insufficient.
- [x] Add `steady_ns` or equivalent monotonic emission time to `ph_frame` logs.
- [x] Add local LibriSpeech `test-clean` discovery and manifest generation.
- [x] Add automatic transcript-to-ARPABET and forced-alignment label generation.
- [x] Add Linux virtual-audio setup validation.
- [x] Add real-time playback driver for selected clips.
- [x] Add live-log-to-prediction conversion.
- [x] Add metrics/report generation.
- [x] Add backend selection/comparison workflow.
- [ ] Re-run the full plan on Windows after Linux passes.
