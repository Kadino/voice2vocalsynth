#!/usr/bin/env bash
# CTest harness for phoneme bakeoff matrix validation and automatic verification.

set -euo pipefail

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "PhonemeBakeoffMatrixVerifyTests: skipped (Linux-only)"
  exit 0
fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${VOICE2VOCALSYNTH_BUILD_DIR:-${REPO_ROOT}/build}"
SCRIPT_DIR="${REPO_ROOT}/scripts"
MATRIX_PATH="${REPO_ROOT}/config/phoneme_bakeoff_matrix.json"

if [[ -z "${VOICE2VOCALSYNTH_BUILD_DIR:-}" ]]; then
  echo "error: VOICE2VOCALSYNTH_BUILD_DIR must be set by CTest" >&2
  exit 1
fi

for tool in python3 ffmpeg; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    echo "PhonemeBakeoffMatrixVerifyTests: skipped (missing ${tool})"
    exit 0
  fi
done

WORK_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/v2vs-bakeoff-matrix.XXXXXX")"
VERIFY_ROOT="${WORK_ROOT}/verify"
REPORT_PATH="${WORK_ROOT}/bakeoff-verify-report.json"
PLAN_PATH="${WORK_ROOT}/bakeoff-verify-plan.json"

cleanup() {
  rm -rf "${WORK_ROOT}"
}
trap cleanup EXIT

export LIVE_PHONEME_VERIFY_ROOT="${VERIFY_ROOT}"
export VOICE2VOCALSYNTH_BUILD_DIR="${BUILD_DIR}"

python3 "${SCRIPT_DIR}/validate_phoneme_bakeoff_matrix.py" "${MATRIX_PATH}"

dry_output="$("${SCRIPT_DIR}/run_phoneme_bakeoff_verify_linux.sh" \
  --dry-run \
  --output "${PLAN_PATH}" \
  2>&1)"
if [[ ! -f "${PLAN_PATH}" ]]; then
  echo "error: bakeoff verify dry-run did not write plan" >&2
  printf '%s\n' "${dry_output}" >&2
  exit 1
fi
if ! printf '%s\n' "${dry_output}" | grep -q "Dry run complete:"; then
  echo "error: bakeoff verify dry-run did not report completion" >&2
  printf '%s\n' "${dry_output}" >&2
  exit 1
fi

python3 - "${PLAN_PATH}" <<'PY'
import json, sys

plan = json.load(open(sys.argv[1], encoding="utf-8"))
assert plan["dryRun"] is True
offline = plan["offlineBakeoff"]["backends"]
live = plan["liveDryRun"]["backends"]
assert "placeholder" in offline
assert "pocketsphinx" in offline
assert "onnx_phoneme" not in offline
assert set(live) == {"placeholder", "pocketsphinx", "onnx_phoneme"}
PY

BAKEOFF_BIN="${BUILD_DIR}/Voice2VocalSynthPhonemeBakeoff"
if [[ -x "${BAKEOFF_BIN}" ]]; then
  verify_output="$("${SCRIPT_DIR}/run_phoneme_bakeoff_verify_linux.sh" \
    --offline \
    --live-dry-run \
    --skip-build \
    --subset 1 \
    --output "${REPORT_PATH}" \
    2>&1)"
  if [[ ! -f "${REPORT_PATH}" ]]; then
    echo "error: bakeoff verify report was not written" >&2
    printf '%s\n' "${verify_output}" >&2
    exit 1
  fi
  python3 - "${REPORT_PATH}" <<'PY'
import json, sys

report = json.load(open(sys.argv[1], encoding="utf-8"))
assert report["offlineBakeoff"]["status"] == "passed"
assert report["liveDryRun"]["status"] == "passed"
entries = report["offlineBakeoff"]["report"]["entries"]
names = {entry["backendName"] for entry in entries}
assert "placeholder_pitch_gate" in names
assert "pocketsphinx_allphone" in names
PY
else
  echo "PhonemeBakeoffMatrixVerifyTests: skipped offline execution (binary missing)"
fi

echo "PhonemeBakeoffMatrixVerifyTests passed"
