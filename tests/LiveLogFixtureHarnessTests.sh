#!/usr/bin/env bash
# Headless phases 5–7 harness: fixture JUCE substitute + scorer (no JUCE build).

set -euo pipefail

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "LiveLogFixtureHarnessTests: skipped (Linux-only)"
  exit 0
fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${VOICE2VOCALSYNTH_BUILD_DIR:-${REPO_ROOT}/build}"
SCRIPT_DIR="${REPO_ROOT}/scripts"

if [[ -z "${VOICE2VOCALSYNTH_BUILD_DIR:-}" ]]; then
  echo "error: VOICE2VOCALSYNTH_BUILD_DIR must be set by CTest" >&2
  exit 1
fi

FIXTURE_APP="${BUILD_DIR}/Voice2VocalSynthLiveLogFixture"
SCORER_BIN="${BUILD_DIR}/Voice2VocalSynthLivePhonemeVerify"
CONVERT_BIN="${BUILD_DIR}/Voice2VocalSynthMfaLabelConvert"

for tool in ffmpeg ffprobe python3 rg; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    echo "LiveLogFixtureHarnessTests: skipped (missing ${tool})"
    exit 0
  fi
done

if [[ ! -x "${FIXTURE_APP}" || ! -x "${SCORER_BIN}" || ! -x "${CONVERT_BIN}" ]]; then
  echo "LiveLogFixtureHarnessTests: skipped (fixture app or scorer binary missing)"
  exit 0
fi

WORK_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/v2vs-live-log-fixture-harness.XXXXXX")"
VERIFY_ROOT="${WORK_ROOT}/verify"
DATASET_ROOT="${VERIFY_ROOT}/datasets/LibriSpeech/test-clean"
LABELS_ROOT="${VERIFY_ROOT}/labels/librispeech-test-clean"
RUN_DIR="${WORK_ROOT}/run"
UTTERANCE_ID="1089-134686-0000"

cleanup() {
  if [[ -n "${shell_pid:-}" ]] && kill -0 "${shell_pid}" 2>/dev/null; then
    kill "${shell_pid}" 2>/dev/null || true
  fi
  rm -rf "${WORK_ROOT}"
}
trap cleanup EXIT

mkdir -p "${VERIFY_ROOT}/runs" "${LABELS_ROOT}"
cp "${REPO_ROOT}/tests/fixtures/linux-audio/virtual_audio_manifest.json" \
  "${VERIFY_ROOT}/linux-virtual-audio.json"

CHAPTER="${DATASET_ROOT}/1089/134686"
mkdir -p "${CHAPTER}"
ffmpeg -y -nostdin -loglevel error \
  -f lavfi -i "sine=frequency=440:duration=0.55" \
  -c:a flac "${CHAPTER}/${UTTERANCE_ID}.flac"
printf '%s FIRST UTTERANCE\n' "${UTTERANCE_ID}" > "${CHAPTER}/1089-134686.trans.txt"

TEXTGRID_ROOT="${WORK_ROOT}/textgrids"
mkdir -p "${TEXTGRID_ROOT}"
cp "${REPO_ROOT}/tests/fixtures/mfa/sample_phones.TextGrid" \
  "${TEXTGRID_ROOT}/${UTTERANCE_ID}.TextGrid"
"${CONVERT_BIN}" --convert-textgrids \
  --textgrid-root "${TEXTGRID_ROOT}" \
  --labels-root "${LABELS_ROOT}" \
  --verify-root "${VERIFY_ROOT}" >/dev/null

export LIVE_PHONEME_VERIFY_ROOT="${VERIFY_ROOT}"
export LIBRISPEECH_TEST_CLEAN_ROOT="${DATASET_ROOT}"
export VOICE2VOCALSYNTH_BUILD_DIR="${BUILD_DIR}"

"${SCRIPT_DIR}/play_librispeech_clips_linux.sh" \
  --dry-run \
  --utterance-id "${UTTERANCE_ID}" \
  --run-dir "${RUN_DIR}" >/dev/null

PLAYBACK_MANIFEST="${RUN_DIR}/playback-manifest.json"
python3 - "${PLAYBACK_MANIFEST}" <<'PY'
import json, sys

path = sys.argv[1]
plan = json.load(open(path, encoding="utf-8"))
anchor = 2_000_000_000
plan["playbackStartedSteadyNs"] = anchor
for clip in plan["clips"]:
    clip["playbackStartedSteadyNs"] = anchor
json.dump(plan, open(path, "w", encoding="utf-8"), indent=2)
PY

CAPTURE_DEVICE="$(python3 - "${VERIFY_ROOT}/linux-virtual-audio.json" <<'PY'
import json, sys
print(json.load(open(sys.argv[1], encoding="utf-8"))["captureDevice"])
PY
)"

"${FIXTURE_APP}" \
  --live-log-export \
  --live-log-out "${RUN_DIR}/live-log.jsonl" \
  --phoneme-backend pocketsphinx \
  --capture-device "${CAPTURE_DEVICE}" \
  --quit-file "${RUN_DIR}/quit-request" \
  >"${RUN_DIR}/shell.stdout.log" 2>"${RUN_DIR}/shell.stderr.log" &
shell_pid=$!

for _ in $(seq 1 50); do
  if [[ -f "${RUN_DIR}/live-log.jsonl" ]] &&
     rg -q '"startup_ok":true' "${RUN_DIR}/live-log.jsonl"; then
    break
  fi
  if ! kill -0 "${shell_pid}" 2>/dev/null; then
    echo "error: headless fixture app exited before writing live-log" >&2
    wait "${shell_pid}" || true
    exit 1
  fi
  sleep 0.1
done

if ! rg -q '"kind":"ph_frame"' "${RUN_DIR}/live-log.jsonl"; then
  echo "error: fixture live-log is missing ph_frame records" >&2
  exit 1
fi

: > "${RUN_DIR}/quit-request"
wait "${shell_pid}"
shell_pid=""

set +e
"${SCORER_BIN}" \
  --live-log "${RUN_DIR}/live-log.jsonl" \
  --playback-manifest "${PLAYBACK_MANIFEST}" \
  --labels-root "${LABELS_ROOT}" \
  --backend pocketsphinx \
  --predictions-out "${RUN_DIR}/predictions.json" \
  --metrics-out "${RUN_DIR}/metrics.json" \
  --report-out "${RUN_DIR}/report.md" \
  --max-e2e-latency-ms 1000 \
  --min-f1 0.5 \
  --max-mean-onset-error-ms 50 \
  --max-p95-onset-error-ms 50 \
  --max-mean-end-error-ms 50 \
  --max-p95-end-error-ms 50 \
  --max-mean-duration-error-ms 50 \
  --max-missed-consonant-rate 0.5
score_status=$?
set -e

if [[ "${score_status}" -ne 0 ]]; then
  echo "error: headless phase 7 scorer returned ${score_status}" >&2
  exit 1
fi
for artifact in predictions.json metrics.json report.md; do
  if [[ ! -s "${RUN_DIR}/${artifact}" ]]; then
    echo "error: missing scorer artifact ${artifact}" >&2
    exit 1
  fi
done

echo "LiveLogFixtureHarnessTests passed"
