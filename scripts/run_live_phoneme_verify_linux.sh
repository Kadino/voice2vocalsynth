#!/usr/bin/env bash
# End-to-end Linux live phoneme verification through the JUCE capture path.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${VOICE2VOCALSYNTH_BUILD_DIR:-${REPO_ROOT}/build}"
export VOICE2VOCALSYNTH_BUILD_DIR="${BUILD_DIR}"
VERIFY_ROOT="${LIVE_PHONEME_VERIFY_ROOT:-${HOME}/.local/share/Voice2VocalSynth/LivePhonemeVerify}"
LABELS_ROOT="${VERIFY_ROOT}/labels/librispeech-test-clean"
AUDIO_MANIFEST="${VERIFY_ROOT}/linux-virtual-audio.json"
SCORER_BIN="${BUILD_DIR}/Voice2VocalSynthLivePhonemeVerify"

BACKEND="pocketsphinx"
SUBSET=20
UTTERANCE_ID=""
RUN_DIR=""
SKIP_SETUP=0
SKIP_AUDIO_PROBE=0
DRY_RUN=0
POCKETSPHINX_MODEL_ROOT=""
ONNX_MODEL=""
ONNX_CONFIG=""

# Provisional first-run gates. Tighten these from observed MFA alignment noise;
# they are explicit so a backend can never pass on latency alone.
MAX_E2E_MS="${LIVE_VERIFY_MAX_E2E_MS:-1000}"
MIN_F1="${LIVE_VERIFY_MIN_F1:-0.20}"
MAX_MEAN_ONSET_MS="${LIVE_VERIFY_MAX_MEAN_ONSET_MS:-150}"
MAX_P95_ONSET_MS="${LIVE_VERIFY_MAX_P95_ONSET_MS:-300}"
MAX_MEAN_END_MS="${LIVE_VERIFY_MAX_MEAN_END_MS:-200}"
MAX_P95_END_MS="${LIVE_VERIFY_MAX_P95_END_MS:-350}"
MAX_MEAN_DURATION_MS="${LIVE_VERIFY_MAX_MEAN_DURATION_MS:-250}"
MAX_MISSED_CONSONANT_RATE="${LIVE_VERIFY_MAX_MISSED_CONSONANT_RATE:-0.85}"

usage() {
  cat <<EOF
Usage: $(basename "$0") [--backend pocketsphinx|onnx_phoneme|placeholder]
       [--subset N | --all | --utterance-id ID] [--run-dir DIR]
       [--pocketsphinx-model-root DIR] [--onnx-model FILE --onnx-config FILE]
       [--skip-setup] [--skip-audio-probe] [--dry-run]

Runs LibriSpeech at 1.0x through validated Linux virtual audio, captures the
selected backend in the JUCE shell, scores only live ph_frame records, writes
predictions.json, metrics.json, and report.md, and returns 3 on gate failure.

PocketSphinx 5.1.1 and its US-English all-phone model are fetched by CMake.
The placeholder is a negative baseline and cannot pass.

Temporal gate environment overrides:
  LIVE_VERIFY_MIN_F1
  LIVE_VERIFY_MAX_MEAN_ONSET_MS
  LIVE_VERIFY_MAX_P95_ONSET_MS
  LIVE_VERIFY_MAX_MEAN_END_MS
  LIVE_VERIFY_MAX_P95_END_MS
  LIVE_VERIFY_MAX_MEAN_DURATION_MS
  LIVE_VERIFY_MAX_MISSED_CONSONANT_RATE
  LIVE_VERIFY_MAX_E2E_MS
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --help|-h) usage; exit 0 ;;
    --backend) BACKEND="${2:?missing value for --backend}"; shift 2 ;;
    --subset) SUBSET="${2:?missing value for --subset}"; shift 2 ;;
    --all) SUBSET=0; shift ;;
    --utterance-id) UTTERANCE_ID="${2:?missing value for --utterance-id}"; shift 2 ;;
    --run-dir) RUN_DIR="${2:?missing value for --run-dir}"; shift 2 ;;
    --pocketsphinx-model-root)
      POCKETSPHINX_MODEL_ROOT="${2:?missing value for --pocketsphinx-model-root}"; shift 2 ;;
    --onnx-model) ONNX_MODEL="${2:?missing value for --onnx-model}"; shift 2 ;;
    --onnx-config) ONNX_CONFIG="${2:?missing value for --onnx-config}"; shift 2 ;;
    --skip-setup) SKIP_SETUP=1; shift ;;
    --skip-audio-probe) SKIP_AUDIO_PROBE=1; shift ;;
    --dry-run) DRY_RUN=1; shift ;;
    *) echo "error: unknown argument: $1" >&2; usage >&2; exit 1 ;;
  esac
done

case "${BACKEND}" in
  pocketsphinx|onnx_phoneme|placeholder) ;;
  *) echo "error: unsupported backend: ${BACKEND}" >&2; exit 1 ;;
esac
if [[ "${BACKEND}" == "onnx_phoneme" && -z "${ONNX_MODEL}" ]]; then
  echo "error: onnx_phoneme verification requires --onnx-model (the identity fixture is invalid)" >&2
  exit 1
fi
if [[ "$(uname -s)" != "Linux" ]]; then
  echo "error: this workflow currently supports Linux only; Windows validation remains pending" >&2
  exit 1
fi
if ! command -v rg >/dev/null 2>&1; then
  echo "error: ripgrep (rg) is required for live-log startup detection" >&2
  exit 1
fi

mkdir -p "${BUILD_DIR}" "${VERIFY_ROOT}/runs"
cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" -DVOICE2VOCALSYNTH_BUILD_JUCE_APP=ON
cmake --build "${BUILD_DIR}" \
  --target Voice2VocalSynthApp Voice2VocalSynthLivePhonemeVerify \
           Voice2VocalSynthLibriSpeechSetup Voice2VocalSynthMfaLabelConvert \
           Voice2VocalSynthLinuxAudioValidate Voice2VocalSynthLibriSpeechPlayback \
  -j"$(nproc)"

APP_BIN="${VOICE2VOCALSYNTH_APP_BIN:-}"
if [[ -z "${APP_BIN}" ]]; then
  for candidate in \
    "${BUILD_DIR}/Voice2VocalSynthApp" \
    "${BUILD_DIR}/apps/juce-shell/Voice2VocalSynthApp_artefacts/Voice2VocalSynth" \
    "${BUILD_DIR}/apps/juce-shell/Voice2VocalSynthApp_artefacts/Release/Voice2VocalSynth" \
    "${BUILD_DIR}/apps/juce-shell/Voice2VocalSynthApp_artefacts/Debug/Voice2VocalSynth"; do
    if [[ -x "${candidate}" ]]; then
      APP_BIN="${candidate}"
      break
    fi
  done
fi
if [[ -z "${APP_BIN}" || ! -x "${APP_BIN}" ]]; then
  echo "error: unable to locate the built JUCE shell; set VOICE2VOCALSYNTH_APP_BIN" >&2
  exit 1
fi

if [[ "${SKIP_SETUP}" -eq 0 ]]; then
  "${SCRIPT_DIR}/setup_librispeech_test_clean.sh" --verify
fi

audio_args=(--check --write-manifest)
if [[ "${SKIP_AUDIO_PROBE}" -eq 0 ]]; then
  audio_args+=(--probe)
fi
"${SCRIPT_DIR}/validate_linux_virtual_audio.sh" "${audio_args[@]}" >/dev/null

CAPTURE_DEVICE="$(python3 - "${AUDIO_MANIFEST}" <<'PY'
import json, sys
print(json.load(open(sys.argv[1], encoding="utf-8"))["captureDevice"])
PY
)"

if [[ -z "${RUN_DIR}" ]]; then
  RUN_DIR="${VERIFY_ROOT}/runs/$(date -u +%Y%m%dT%H%M%SZ)-${BACKEND}"
fi
if [[ -e "${RUN_DIR}" ]]; then
  if [[ ! -d "${RUN_DIR}" ]]; then
    echo "error: run path exists and is not a directory: ${RUN_DIR}" >&2
    exit 1
  fi
  if compgen -G "${RUN_DIR}/*" >/dev/null; then
    echo "error: refusing to reuse nonempty run directory: ${RUN_DIR}" >&2
    exit 1
  fi
fi
mkdir -p "${RUN_DIR}"

selection_args=(--run-dir "${RUN_DIR}")
if [[ -n "${UTTERANCE_ID}" ]]; then
  selection_args+=(--utterance-id "${UTTERANCE_ID}")
elif [[ "${SUBSET}" -eq 0 ]]; then
  selection_args+=(--all)
else
  selection_args+=(--subset "${SUBSET}")
fi
"${SCRIPT_DIR}/play_librispeech_clips_linux.sh" --dry-run "${selection_args[@]}"

PLAYBACK_MANIFEST="${RUN_DIR}/playback-manifest.json"

echo "RUN_DIR=${RUN_DIR}"
echo "backend=${BACKEND} capture=${CAPTURE_DEVICE}"
if [[ "${DRY_RUN}" -eq 1 ]]; then
  echo "Dry run complete: ${PLAYBACK_MANIFEST}"
  exit 0
fi

missing_labels="$(python3 - "${PLAYBACK_MANIFEST}" "${LABELS_ROOT}" <<'PY'
import json, pathlib, sys
plan = json.load(open(sys.argv[1], encoding="utf-8"))
root = pathlib.Path(sys.argv[2])
print("\n".join(
    clip["utteranceId"]
    for clip in plan["clips"]
    if not (root / f"{clip['utteranceId']}.json").is_file()
))
PY
)"
if [[ -n "${missing_labels}" ]]; then
  if [[ "${SKIP_SETUP}" -eq 1 ]]; then
    echo "error: reference labels are missing for selected clips:" >&2
    printf '%s\n' "${missing_labels}" >&2
    exit 1
  fi
  if [[ -n "${UTTERANCE_ID}" ]]; then
    "${SCRIPT_DIR}/generate_librispeech_mfa_labels.sh" --utterance-id "${UTTERANCE_ID}"
  elif [[ "${SUBSET}" -eq 0 ]]; then
    "${SCRIPT_DIR}/generate_librispeech_mfa_labels.sh" --all
  else
    "${SCRIPT_DIR}/generate_librispeech_mfa_labels.sh" --subset "${SUBSET}"
  fi
fi

shell_args=(
  --live-log-export
  --live-log-out "${RUN_DIR}/live-log.jsonl"
  --phoneme-backend "${BACKEND}"
  --capture-device "${CAPTURE_DEVICE}"
  --quit-file "${RUN_DIR}/quit-request"
)
if [[ -n "${POCKETSPHINX_MODEL_ROOT}" ]]; then
  shell_args+=(--pocketsphinx-model-root "${POCKETSPHINX_MODEL_ROOT}")
fi
if [[ -n "${ONNX_MODEL}" ]]; then
  shell_args+=(--onnx-model "${ONNX_MODEL}")
fi
if [[ -n "${ONNX_CONFIG}" ]]; then
  shell_args+=(--onnx-config "${ONNX_CONFIG}")
fi

"${APP_BIN}" "${shell_args[@]}" >"${RUN_DIR}/shell.stdout.log" 2>"${RUN_DIR}/shell.stderr.log" &
shell_pid=$!
cleanup() {
  if kill -0 "${shell_pid}" 2>/dev/null; then
    kill "${shell_pid}" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

for _ in $(seq 1 150); do
  if [[ -f "${RUN_DIR}/live-log.jsonl" ]] &&
     rg -q '"kind":"session_start"' "${RUN_DIR}/live-log.jsonl"; then
    break
  fi
  if ! kill -0 "${shell_pid}" 2>/dev/null; then
    echo "error: JUCE shell exited before live capture started" >&2
    wait "${shell_pid}" || true
    exit 1
  fi
  sleep 0.2
done
if [[ ! -f "${RUN_DIR}/live-log.jsonl" ]] ||
   ! rg -q '"kind":"session_start"' "${RUN_DIR}/live-log.jsonl"; then
  echo "error: timed out waiting for JUCE live-log startup" >&2
  exit 1
fi
if ! rg -q '"startup_ok":true' "${RUN_DIR}/live-log.jsonl"; then
  echo "error: JUCE shell rejected the requested backend or capture device" >&2
  rg '"kind":"session_start"' "${RUN_DIR}/live-log.jsonl" >&2 || true
  exit 1
fi

"${SCRIPT_DIR}/play_librispeech_clips_linux.sh" "${selection_args[@]}"
# Let trailing virtual-input silence finalize the decoder's last partial phone.
sleep 1
: > "${RUN_DIR}/quit-request"
wait "${shell_pid}"
trap - EXIT INT TERM

set +e
"${SCORER_BIN}" \
  --live-log "${RUN_DIR}/live-log.jsonl" \
  --playback-manifest "${PLAYBACK_MANIFEST}" \
  --labels-root "${LABELS_ROOT}" \
  --backend "${BACKEND}" \
  --max-e2e-latency-ms "${MAX_E2E_MS}" \
  --min-f1 "${MIN_F1}" \
  --max-mean-onset-error-ms "${MAX_MEAN_ONSET_MS}" \
  --max-p95-onset-error-ms "${MAX_P95_ONSET_MS}" \
  --max-mean-end-error-ms "${MAX_MEAN_END_MS}" \
  --max-p95-end-error-ms "${MAX_P95_END_MS}" \
  --max-mean-duration-error-ms "${MAX_MEAN_DURATION_MS}" \
  --max-missed-consonant-rate "${MAX_MISSED_CONSONANT_RATE}"
score_status=$?
set -e

echo "Live verification artifacts: ${RUN_DIR}"
exit "${score_status}"
