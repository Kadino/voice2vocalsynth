# Voice2VocalSynth

Voice2VocalSynth is a private Windows-focused C++ project for live
voice-to-UTAU vocal synthesis experiments.

## Canonical project specification (agent-parseable)

See [`voice2vocalsynth.spec.md`](voice2vocalsynth.spec.md). The **YAML frontmatter** in that file is the canonical, machine-parseable source of truth for project direction, pipeline, latency modes, detection design, and renderer plans. For the **live pipeline implementation roadmap** (milestones, gaps, next steps), see [`docs/live-pipeline-roadmap.md`](docs/live-pipeline-roadmap.md).

## Current core module

The repository currently contains the first phoneme mapping slice:

- ARPABET phoneme normalization with stress digit stripping.
- English phoneme to Japanese CV alias mapping.
- Configurable consonant substitutions such as `TH`.
- Partial CVC fallback for trailing consonants, e.g. `K AE T` maps to
  `ka` plus a shortened final `to` alias.
- Renderer hints for partial finals so later audio code can preserve the
  consonant while attenuating the helper vowel tail.
- Equal-temperament pitch target calculation with raw follow, semitone snap,
  key snap, fixed/default pitch, octave shifting, snap strength, and
  low-confidence fallback handling.
- Optional **ONNX Runtime** integration (`PhonemeOnnxRunner`, CMake
  `VOICE2VOCALSYNTH_WITH_ONNX`): loads a model and runs CPU inference; **Linux x64**
  and **Windows x64** can auto-download Microsoft ONNX Runtime **1.26.x** (see
  `cmake/OnnxRuntime.cmake`), or set `VOICE2VOCALSYNTH_ONNXRUNTIME_ROOT` to an
  extracted tree. Windows builds copy `onnxruntime*.dll` next to linked executables.
  `PhonemeOnnxAsyncRunner` runs inference on a worker thread and tags each result
  with the job’s stream time plus a `steady_clock` completion time for latency
  alignment. Preset JSON includes `phonemeOnnx` (`enabled`, `useRepositoryTestFixture`,
  `modelPath`); CMake cache `VOICE2VOCALSYNTH_PHONEME_REPOSITORY_FIXTURE` selects the
  in-repo dummy model path baked into the binary for that mode. See
  `tests/fixtures/onnx/PROVENANCE.md` for the checked-in dummy ONNX used in tests.
- Recent reliable pitch tracking for low-confidence fallback means.
- Basic `oto.ini` parsing for UTAU voicebank aliases and timing fields.
- Voicebank alias resolution that chooses the first available mapper candidate
  and reports missing candidates for UI/debug output.
- Voicebank alias style is auto-detected from the loaded alias inventory.
- Voicebank folder scanning for recursive `oto.ini` discovery, relative sample
  path normalization, alias-index construction, and an inferred **bank root
  recording pitch** (median of `prefix.map` note names, else ASCII note tokens
  in aliases such as `C4_ka`) exposed on `VoicebankScanResult` for render defaults.
- Prefix map discovery/parsing for multipitch prefix/suffix alias
  selection and prefix/suffix-aware alias candidate expansion.
- Voicebank mapping planning that combines ARPABET mapping, prefix/suffix
  expansion, alias resolution, and missing-alias diagnostics.
- Render planning that turns resolved aliases into scheduled render events with
  WAV paths, oto timing, render hints, target pitch, and skipped-event
  diagnostics.
- Offline renderer v1: 16-bit PCM WAV load (mono/stereo to mono), oto
  offset/cutoff region extract, linear time-stretch to event duration, timeline
  mix into a float buffer, and UTAU-style **overlap** into the previous note
  with a linear crossfade. **Target pitch** uses naive linear resampling against
  an assumed recording fundamental (`RenderEvent` override,
  `OfflineRenderOptions::defaultSourceRecordingFrequencyHz`, or the scanned bank
  root in the JUCE offline test). **Pitch-up** uses a consonant pass, a
  **looped sustain** band inside the oto window (after `consonantMs`, reserving
  an inner tail), then a one-shot **trailing** read; otherwise the legacy linear
  path applies (with truncation warnings when the read passes cutoff).
- 16-bit mono PCM WAV export (`PcmWavWriter`) for offline renders; JUCE shell
  includes an **Offline render test** flow with phrase **ARPABET** and **note**
  fields persisted in `shell_settings.json` (`offlinePhonemes`, `offlineNote`).
- Debug timeline JSON export for pitch, render events, skipped events, and
  missing aliases.
- Latency budget presets and end-to-end monitoring latency breakdowns for
  analysis, stabilization, render scheduling, and device latency.
- App preset settings for audio routing, voicebank selection, pitch behavior,
  latency mode, and recording/debug options.
- Editable JSON preset import/export for the settings model. The UI can use the
  same model while users can still edit preset files by hand.
- **`PhonemeTemporalStabilizer`** (`PhonemeTemporalObservation` → committed **`PhonemeFrame`** segments) with unit tests; JUCE live log includes **`ph_frame`** lines driven by a **pitch-gated placeholder** (testing) **in parallel** with optional **ONNX stub** jobs—this hybrid stays for exercising stabilizer boundaries and async inference without a real phoneme head yet.
- **`IPhonemeBackend`** defines the swappable phoneme detection contract;
  **`PlaceholderPitchPhonemeBackend`** implements the current pitch-gated `AH`
  debug path, and **`PhonemeEvaluation`** provides initial precision/recall/F1
  and onset-error metrics for comparing future backends against labeled frames.
- **`WhistleDetector`** (Goertzel HNR-style proxy) runs in parallel with phoneme ONNX; live log emits `whistle` / `whistle_edge` JSON and bypasses the phoneme stabilizer placeholder while whistle mode is active.
- **`VoiceActivityDetector`** emits timestamped **`speech_onset`** / **`speech_end`** on the stream clock; the JUCE live log records them as **`vad`** JSON.
- **`InferenceLatencyTracker`** maintains a clamped moving estimate of ONNX queue+inference lag; shell **`onnx`** lines include `lag_ms` and `lag_est_ms`.
- **`PlaybackBoundaryMapper`** and **`UtteranceSustainReleasePolicy`** map analysis boundaries to playback-time sustain release (`sustain_release` JSON). Offline render honors `RenderEvent::perceivedUtteranceEndSeconds` to truncate sustain loops.
- **`PhonemeMappingConfigLoader`** reads `phoneme_to_japanese.json` (user app-data, optional `shell_settings.json` `phonemeMappingPath`, or repo `config/phoneme_to_japanese.json`) and merges overrides onto built-in defaults for **`PhonemeFallbackMapper`** / offline mapping.
- **`LoopbackLatencyMeasurer`** injects a short probe on the output pass-through and estimates round-trip delay via normalized cross-correlation on the input (requires output→input loopback). The JUCE shell exposes **Measure loopback latency**, logs `latency_measure` JSON, and uses the measurement to augment effective end-to-end latency for playback boundary mapping when valid.

## Build

Requires CMake **3.22+** (JUCE’s build scripts). The **Voice2VocalSynth** target is a JUCE standalone shell
(Windows). To skip fetching JUCE (core library and tests only), configure with
`-DVOICE2VOCALSYNTH_BUILD_JUCE_APP=OFF`. To skip ONNX Runtime (stub phoneme runner
only), use `-DVOICE2VOCALSYNTH_WITH_ONNX=OFF`. On hosts where auto-download is not
implemented (for example non-x64), set `VOICE2VOCALSYNTH_ONNXRUNTIME_ROOT` to an
extracted official ONNX Runtime tree (`include/` + `lib/`). Override the checked-in
phoneme test fixture path with `-DVOICE2VOCALSYNTH_PHONEME_REPOSITORY_FIXTURE=...`
when pointing presets at a different `.onnx` file for development.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

On **Linux**, building the JUCE shell may require development packages for **GTK 3**, **WebKitGTK 4.1**, **libcurl**, **ALSA**, **X11**, **FreeType**, and **OpenGL** (the CI image here links `PkgConfig::gtk+-3.0`, `webkit2gtk-4.1`, and `libcurl` explicitly in `apps/juce-shell/CMakeLists.txt` after JUCE’s own dependency probe).

## License

This repository is private-use software. No permission is granted to copy,
modify, distribute, sublicense, or use this code except with prior written
permission from the copyright holder.

See [LICENSE](LICENSE) for the full terms.
