# Phoneme backend bakeoff matrix

Machine-readable source: [`config/phoneme_bakeoff_matrix.json`](../config/phoneme_bakeoff_matrix.json).

Automatic verification: [`scripts/run_phoneme_bakeoff_verify_linux.sh`](../scripts/run_phoneme_bakeoff_verify_linux.sh).

## Selection criteria

Backends are scored against the live verification plan criteria:

1. Streaming-compatible input contract
2. ARPABET output or deterministic mapping to ARPABET
3. Stable timestamps on the capture stream clock
4. Confidence normalized for `PhonemeTemporalStabilizer`
5. Practical CPU latency on Linux
6. Improved temporal correctness versus existing candidates

## Integrated backends (runnable today)

| ID | Bakeoff / shell name | License | Streaming | ARPABET | Live gates |
|----|----------------------|---------|-----------|---------|------------|
| `placeholder_pitch_gate` | `placeholder` | project | debug | pitch-gated AH | cannot pass |
| `pocketsphinx_allphone` | `pocketsphinx` | BSD-2-Clause | true incremental | direct | can pass |
| `phoneme_onnx` | `onnx_phoneme` | adapter | model-dependent | model-dependent | cannot pass with identity fixture |

## Candidate backends (permissive, compare when integrated)

| ID | License | Streaming | ARPABET | Integration path | Priority |
|----|---------|-----------|---------|------------------|----------|
| `wav2vec2_espeak_cv` | Apache-2.0 | windowed | IPA→ARPABET map | ONNX export → `PhonemeOnnxBackend` | 2 |
| `wav2vec2_timit_arpabet` | Apache-2.0 | windowed | TIMIT phoneme set | ONNX export → `PhonemeOnnxBackend` | 3 |
| `wavlm_arpabet_trainable` | Apache-2.0 | windowed | direct after fine-tune | train on `librispeech-arpabet-processed` | 4 |
| `kaldi_phone_loop` | Apache-2.0 | true incremental | phone-loop FST | future native/subprocess | 5 |
| `julius_allphone` | BSD-3-Clause | true incremental | loop grammar | future native C++ | 6 |
| `sherpa_onnx_phoneme_ctc` | Apache-2.0 | true incremental | after icefall train | ONNX or sherpa C API | 7 |

Windowed neural candidates must **not** be described as true streaming in reports.

## Excluded (license or role mismatch)

| ID | License | Reason |
|----|---------|--------|
| Allosaurus | GPL-3.0 | Copyleft; IPA output |
| phonemizer / espeak-ng | GPL-3.0 | Text G2P, not acoustic |
| MFA | MIT | Forced alignment; reference labels only |
| Vosk phones patch | Apache-2.0 | Phoneme mode not upstream |

## Verification modes

`run_phoneme_bakeoff_verify_linux.sh` supports:

- `--validate-only` — schema-check the matrix JSON
- `--dry-run` — emit `bakeoff-verify-plan.json` (default in CTest)
- `--offline` — run `Voice2VocalSynthPhonemeBakeoff` for integrated offline backends
- `--live-dry-run` — run `compare_live_phoneme_backends_linux.sh --dry-run` for integrated live backends

Formal live scoring still requires the full JUCE capture path (`run_live_phoneme_verify_linux.sh` without `--dry-run`).
