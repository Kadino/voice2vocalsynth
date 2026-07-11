#!/usr/bin/env bash
# CTest harness for run_live_phoneme_verify_linux.sh --dry-run (spec futureHardening).

set -euo pipefail

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "LiveVerifyOrchestrationDryRunTests: skipped (Linux-only)"
  exit 0
fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${VOICE2VOCALSYNTH_BUILD_DIR:-${REPO_ROOT}/build}"
SCRIPT_DIR="${REPO_ROOT}/scripts"

if [[ -z "${VOICE2VOCALSYNTH_BUILD_DIR:-}" ]]; then
  echo "error: VOICE2VOCALSYNTH_BUILD_DIR must be set by CTest" >&2
  exit 1
fi

for tool in ffmpeg ffprobe python3 rg; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    echo "LiveVerifyOrchestrationDryRunTests: skipped (missing ${tool})"
    exit 0
  fi
done

WORK_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/v2vs-live-verify-dry-run.XXXXXX")"
VERIFY_ROOT="${WORK_ROOT}/verify"
DATASET_ROOT="${VERIFY_ROOT}/datasets/LibriSpeech/test-clean"
RUN_DIR="${WORK_ROOT}/run"
UTTERANCE_ID="1089-134686-0000"
STUB_APP="${WORK_ROOT}/stub-voice2vocalsynth-app"

cleanup() {
  rm -rf "${WORK_ROOT}"
}
trap cleanup EXIT

mkdir -p "${VERIFY_ROOT}/runs"
cp "${REPO_ROOT}/tests/fixtures/linux-audio/virtual_audio_manifest.json" \
  "${VERIFY_ROOT}/linux-virtual-audio.json"

CHAPTER="${DATASET_ROOT}/1089/134686"
mkdir -p "${CHAPTER}"
ffmpeg -y -nostdin -loglevel error \
  -f lavfi -i "sine=frequency=440:duration=0.25" \
  -c:a flac "${CHAPTER}/${UTTERANCE_ID}.flac"
printf '%s FIRST UTTERANCE\n' "${UTTERANCE_ID}" > "${CHAPTER}/1089-134686.trans.txt"

cat > "${STUB_APP}" <<'EOF'
#!/usr/bin/env bash
echo "stub Voice2VocalSynthApp for orchestration dry-run"
exit 0
EOF
chmod +x "${STUB_APP}"

export LIVE_PHONEME_VERIFY_ROOT="${VERIFY_ROOT}"
export LIBRISPEECH_TEST_CLEAN_ROOT="${DATASET_ROOT}"
export VOICE2VOCALSYNTH_BUILD_DIR="${BUILD_DIR}"
export VOICE2VOCALSYNTH_APP_BIN="${STUB_APP}"

output="$("${SCRIPT_DIR}/run_live_phoneme_verify_linux.sh" \
  --skip-build \
  --skip-setup \
  --skip-audio-check \
  --skip-audio-probe \
  --dry-run \
  --utterance-id "${UTTERANCE_ID}" \
  --run-dir "${RUN_DIR}" \
  2>&1)"

manifest="${RUN_DIR}/playback-manifest.json"
if [[ ! -f "${manifest}" ]]; then
  echo "error: playback manifest was not written" >&2
  printf '%s\n' "${output}" >&2
  exit 1
fi

python3 - "${manifest}" "${UTTERANCE_ID}" <<'PY'
import json, sys

manifest_path, utterance_id = sys.argv[1], sys.argv[2]
plan = json.load(open(manifest_path, encoding="utf-8"))
assert len(plan["clips"]) == 1
assert plan["clips"][0]["utteranceId"] == utterance_id
assert plan["clips"][0]["durationSeconds"] > 0.0
PY

if ! printf '%s\n' "${output}" | grep -q "Dry run complete:"; then
  echo "error: orchestration script did not report dry-run completion" >&2
  printf '%s\n' "${output}" >&2
  exit 1
fi

echo "LiveVerifyOrchestrationDryRunTests passed"
