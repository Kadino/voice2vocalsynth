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

The playback manifest records a monotonic launch anchor for every clip (rather
than relying only on nominal accumulated durations). This prevents ffmpeg
process startup and teardown time from accumulating as cross-clip label drift;
delivery/startup delay remains visible in the timing metrics instead of being
silently subtracted.

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
- a run without valid `ph_frame` decision-latency and backend-latency samples
  fails closed. The end-to-end value includes P95 capture-to-commit decision
  latency, not only device or loopback latency.

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

## Local environment testing plan

Canonical machine-parseable form: [`voice2vocalsynth.spec.md`](../voice2vocalsynth.spec.md) → `livePhonemeVerification.localTestingPlan`.

This section is the human-readable companion for incrementally verifying every live-path
piece in a local Linux development environment, including dependency bootstrap. It
complements the 37 CTest targets (library logic and fixtures); it is **not** a CI job.

### Principles

1. **Use two build directories** to avoid JUCE/core coupling:
   - `build/` — core library, CLIs, CTest (`-DVOICE2VOCALSYNTH_BUILD_JUCE_APP=OFF`)
   - `build-juce/` — JUCE shell (`-DVOICE2VOCALSYNTH_BUILD_JUCE_APP=ON`)
2. **Keep data outside the repo** at `~/.local/share/Voice2VocalSynth/LivePhonemeVerify/`.
3. **Start small**: one utterance → subset (20) → full corpus.
4. **Record everything** in timestamped run dirs under `runs/`.

### Phase 0 — One-time dependency bootstrap

#### System packages (Ubuntu/Debian)

```sh
sudo apt-get update
sudo apt-get install -y \
  cmake g++ libstdc++-13-dev pkg-config \
  libgtk-3-dev libwebkit2gtk-4.1-dev libcurl4-openssl-dev \
  libasound2-dev libx11-dev libxrandr-dev libxinerama-dev \
  libxcursor-dev libfreetype-dev libgl1-mesa-dev \
  ffmpeg curl python3 \
  pulseaudio-utils ripgrep
```

Optional alternates (only if not using PipeWire/Pulse):

- ALSA loopback: `sudo modprobe snd-aloop`
- JACK: `jackd2` or PipeWire's `pw-jack`

#### PipeWire/Pulse null sink (primary route)

```sh
pactl info

pactl load-module module-null-sink \
  sink_name=LivePhonemeVerify \
  sink_properties=device.description=LivePhonemeVerify
```

Verify:

```sh
pactl list sinks short | grep LivePhonemeVerify
pactl list sources short | grep LivePhonemeVerify.monitor
```

#### Montreal Forced Aligner (reference labels)

```sh
conda create -n mfa -c conda-forge montreal-forced-aligner -y
conda activate mfa

mfa version
mfa model download acoustic english_us_arpa
mfa model download dictionary english_us_arpa
```

Verify:

```sh
scripts/generate_librispeech_mfa_labels.sh --check-mfa
```

#### Build both targets

```sh
CXX=g++ cmake -S . -B build -DVOICE2VOCALSYNTH_BUILD_JUCE_APP=OFF
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure

CXX=g++ cmake -S . -B build-juce -DVOICE2VOCALSYNTH_BUILD_JUCE_APP=ON
cmake --build build-juce --target Voice2VocalSynthApp -j"$(nproc)"
```

Set environment for subsequent steps:

```sh
export VOICE2VOCALSYNTH_BUILD_DIR="$PWD/build-juce"
export VOICE2VOCALSYNTH_APP_BIN="$PWD/build-juce/apps/juce-shell/Voice2VocalSynthApp_artefacts/Voice2VocalSynth"
export LIVE_PHONEME_VERIFY_ROOT="${HOME}/.local/share/Voice2VocalSynth/LivePhonemeVerify"
```

#### Dependency smoke checklist

| Tool | Check | Expected |
|------|-------|----------|
| cmake | `cmake --version` | ≥ 3.22 (≥ 3.25 for PocketSphinx) |
| g++ | `g++ --version` | C++20 capable |
| ffmpeg/ffprobe | `ffmpeg -version && ffprobe -version` | present |
| curl | `curl --version` | present |
| python3 | `python3 --version` | present |
| rg | `rg --version` | present (required by orchestration script; undeclared in README) |
| pactl/parec | `pactl info && parec --help` | session + capture |
| mfa | `mfa version` | present after `conda activate` |
| JUCE app | `test -x "$VOICE2VOCALSYNTH_APP_BIN"` | executable |
| CTest | `ctest --test-dir build -N` | 41 tests |

**Pass criteria:** all checks succeed; null sink exists when using the primary route.

### Phase 1 — LibriSpeech setup

**Gaps covered:** real dataset download and validation (not just synthetic mini-dataset tests).

```sh
scripts/setup_librispeech_test_clean.sh --download
scripts/setup_librispeech_test_clean.sh --verify
scripts/setup_librispeech_test_clean.sh --manifest
```

**Pass criteria:**

- `Voice2VocalSynthLibriSpeechSetup --verify` exits 0
- Manifest at `$LIVE_PHONEME_VERIFY_ROOT/datasets/LibriSpeech/librispeech-test-clean-manifest.json`
- At least one utterance listable: `build/Voice2VocalSynthLibriSpeechSetup --list-utterances --limit 3`

### Phase 2 — MFA reference labels

**Gaps covered:** MFA alignment execution and ffmpeg corpus prep (not just TextGrid fixture conversion).

```sh
UTT_ID="$(build/Voice2VocalSynthLibriSpeechSetup --list-utterances --limit 1 | cut -f1)"
scripts/generate_librispeech_mfa_labels.sh --utterance-id "$UTT_ID"
scripts/generate_librispeech_mfa_labels.sh --subset 20
```

**Pass criteria:**

- Label files at `$LIVE_PHONEME_VERIFY_ROOT/labels/librispeech-test-clean/<utterance-id>.json`
- Manifest records MFA version and `stressDigitsStripped`
- Self-compare one label file via `Voice2VocalSynthPhonemeEval` yields perfect F1

### Phase 3 — Virtual audio routing

**Gaps covered:** live `pactl` detection and audio probe (not fixture parsing only).

```sh
scripts/validate_linux_virtual_audio.sh --check
scripts/validate_linux_virtual_audio.sh --check --probe --write-manifest
```

**Pass criteria:**

- JSON report has `"valid": true`
- `probePassed: true` after `--probe`
- Manifest at `$LIVE_PHONEME_VERIFY_ROOT/linux-virtual-audio.json`

Alternate routes if PipeWire is unavailable:

```sh
scripts/validate_linux_virtual_audio.sh --route alsa --check
scripts/validate_linux_virtual_audio.sh --route jack --check
```

Note: `--probe` is only implemented for `pipewire-loopback`.

### Phase 4 — Playback manifest and real-time ffmpeg

**Gaps covered:** `ffprobe` durations, manifest build, and `ffmpeg -re` output (without JUCE yet).

```sh
RUN_DIR="$LIVE_PHONEME_VERIFY_ROOT/runs/manual-playback-$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$RUN_DIR"

scripts/play_librispeech_clips_linux.sh \
  --dry-run --subset 1 --utterance-id "$UTT_ID" --run-dir "$RUN_DIR"

scripts/play_librispeech_clips_linux.sh \
  --subset 1 --utterance-id "$UTT_ID" --run-dir "$RUN_DIR"
```

**Pass criteria:**

- `playback-manifest.json` contains `routeId`, `playbackDevice`, and clip `flacPath` entries
- Live play completes without ffmpeg errors
- Each clip has non-zero `playbackStartedSteadyNs` after play
- `parec --device=LivePhonemeVerify.monitor -d 1` shows energy during playback

### Phase 5 — JUCE shell live-log export (no playback yet)

**Gaps covered:** JUCE startup, backend load, `session_start` / `startup_ok`, log file I/O, quit-file.

```sh
RUN_DIR="$LIVE_PHONEME_VERIFY_ROOT/runs/manual-juce-startup-$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$RUN_DIR"

CAPTURE="$(python3 -c "import json; print(json.load(open('$LIVE_PHONEME_VERIFY_ROOT/linux-virtual-audio.json'))['captureDevice'])")"

"$VOICE2VOCALSYNTH_APP_BIN" \
  --live-log-export \
  --live-log-out "$RUN_DIR/live-log.jsonl" \
  --phoneme-backend pocketsphinx \
  --capture-device "$CAPTURE" \
  --quit-file "$RUN_DIR/quit-request" \
  >"$RUN_DIR/shell.stdout.log" 2>"$RUN_DIR/shell.stderr.log" &
SHELL_PID=$!

for _ in $(seq 1 50); do
  rg -q '"startup_ok":true' "$RUN_DIR/live-log.jsonl" 2>/dev/null && break
  sleep 0.2
done

: > "$RUN_DIR/quit-request"
wait "$SHELL_PID"
```

**Pass criteria:**

- `live-log.jsonl` contains `session_start` with `startup_ok: true`
- `backend_descriptor` and `device_settings` lines present
- Shell exits cleanly after quit-file

Repeat for `placeholder` and `onnx_phoneme` (requires a **non-identity** ONNX model). Optional: `--auto-loopback-measure` for `latency_measure` JSON.

### Phase 6 — Live backend + stabilizer (tone smoke)

**Gaps covered:** audio callback → backend → `ph_frame` emission without LibriSpeech.

1. Start JUCE export (Phase 5) with `--quit-after-seconds 5`.
2. In another terminal:

```sh
ffmpeg -f lavfi -i sine=frequency=200:duration=3 \
  -f pulse -device LivePhonemeVerify -
```

**Pass criteria:** at least one `ph_frame` or `backend_inference` record; timestamps monotonic; pocketsphinx backend is `pocketsphinx_allphone`.

### Phase 7 — Post-capture scoring (offline)

**Gaps covered:** scorer paths for `onnx` latency lines, `latency_measure`, multi-clip alignment.

```sh
build/Voice2VocalSynthLivePhonemeVerify \
  --live-log "$RUN_DIR/live-log.jsonl" \
  --playback-manifest "$RUN_DIR/playback-manifest.json" \
  --labels-root "$LIVE_PHONEME_VERIFY_ROOT/labels/librispeech-test-clean" \
  --backend pocketsphinx \
  --predictions-out "$RUN_DIR/predictions.json" \
  --metrics-out "$RUN_DIR/metrics.json" \
  --report-out "$RUN_DIR/report.md" \
  --max-e2e-latency-ms 1000 \
  --min-f1 0.20 \
  --max-mean-onset-error-ms 150 \
  --max-p95-onset-error-ms 300 \
  --max-mean-end-error-ms 200 \
  --max-p95-end-error-ms 350 \
  --max-mean-duration-error-ms 250 \
  --max-missed-consonant-rate 0.85
```

**Pass criteria:** exit 0 = gates passed; exit 3 = scored but gates failed; all output files non-empty.

Also run variants for `onnx_phoneme`, `placeholder` gate rejection, multi-clip logs, and `latency_measure`-based E2E.

### Phase 8 — Full orchestration script

**Gaps covered:** bash orchestration, `rg` startup wait, shell↔playback handshake.

```sh
VOICE2VOCALSYNTH_BUILD_DIR=build-juce \
  scripts/run_live_phoneme_verify_linux.sh --dry-run --subset 1 --utterance-id "$UTT_ID"

VOICE2VOCALSYNTH_BUILD_DIR=build-juce \
  scripts/run_live_phoneme_verify_linux.sh --subset 1 --utterance-id "$UTT_ID"

scripts/run_live_phoneme_verify_linux.sh --subset 20
```

**Pass criteria:**

- Run dir contains `live-log.jsonl`, `playback-manifest.json`, `predictions.json`, `metrics.json`, `report.md`, shell logs
- `ph_frame` records present
- Exit **0** = passed gates; **3** = scored but failed gates; **other** = infrastructure failure

**Known issue:** default `build/` with JUCE OFF breaks the script unless `VOICE2VOCALSYNTH_BUILD_DIR` points at `build-juce`.

### Phase 9 — Multi-backend comparison

```sh
scripts/compare_live_phoneme_backends_linux.sh \
  --backends placeholder,pocketsphinx \
  --subset 5 \
  --output "$LIVE_PHONEME_VERIFY_ROOT/runs/comparison-test.json"
```

With ONNX (requires real model):

```sh
scripts/compare_live_phoneme_backends_linux.sh \
  --backends placeholder,pocketsphinx,onnx_phoneme \
  --subset 5 \
  --onnx-model /path/to/model.onnx \
  --onnx-config /path/to/model.phoneme.json
```

**Pass criteria:** `comparison.json` ranks backends; placeholder does not pass; exit 0 only if at least one backend passes all gates.

### Phase 10 — StreamingLiveRenderer (manual)

**Gaps covered:** no unit tests for `StreamingLiveRenderer`.

1. Configure a local UTAU voicebank in `shell_settings.json`.
2. Run Phase 8 with pocketsphinx on a voiced clip.
3. Inspect `live-log.jsonl` for render/sustain events and `live_timeline` debug output.

**Pass criteria:** live synthesis audible or timeline JSON emitted; no missing-alias storms for simple vowel phrases.

### Recommended execution order

```text
Phase 0 Bootstrap
  → Phase 1 LibriSpeech
  → Phase 2 MFA labels
  → Phase 3 Virtual audio
  → Phase 4 Playback
  → Phase 5 JUCE startup
  → Phase 6 Tone smoke
  → Phase 8 Full E2E
  → Phase 7 Scorer variants
  → Phase 9 Compare backends
  → Phase 10 Live synthesis (manual)
```

### Coverage gaps (not in CTest)

| Area | Status |
|------|--------|
| Real-time ffmpeg playback at 1.0x | Manual phases 4, 8 |
| Live virtual-audio host probe | Manual phase 3 |
| MFA alignment execution | Manual phase 2 |
| JUCE live runtime wiring | Manual phases 5–6, 8 |
| `StreamingLiveRenderer` | Manual phase 10 |
| Bash orchestration scripts | Manual phases 8–9 |

### Future hardening

- [x] Document and check `ripgrep` in orchestration script
- [x] Add `LibriSpeechPlaybackCliTests`, `MfaLabelCliTests`, `LinuxVirtualAudioCliTests`
- [x] Add `StreamingLiveRenderer` unit test with temp voicebank
- [x] Extend `LivePhonemeVerificationTests` for `latency_measure` and `onnx` log kinds
- [x] `run_live_phoneme_verify_linux.sh` passes `-DVOICE2VOCALSYNTH_BUILD_JUCE_APP=ON` explicitly
- Optional CI harness for `--dry-run` and fixture-based scorer only
