#!/usr/bin/env bash
# Lightweight CTest harness for bash orchestration --dry-run paths (spec futureHardening).

set -euo pipefail

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "ScriptDryRunTests: skipped (Linux-only orchestration scripts)"
  exit 0
fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${VOICE2VOCALSYNTH_BUILD_DIR:-${REPO_ROOT}/build}"
SCRIPT_DIR="${REPO_ROOT}/scripts"

if [[ -z "${VOICE2VOCALSYNTH_BUILD_DIR:-}" ]]; then
  echo "error: VOICE2VOCALSYNTH_BUILD_DIR must be set by CTest" >&2
  exit 1
fi

for tool in ffmpeg ffprobe python3; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    echo "ScriptDryRunTests: skipped (missing ${tool})"
    exit 0
  fi
done

WORK_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/v2vs-script-dry-run.XXXXXX")"
VERIFY_ROOT="${WORK_ROOT}/verify"
DATASET_ROOT="${VERIFY_ROOT}/datasets/LibriSpeech/test-clean"
RUN_DIR="${WORK_ROOT}/run"
UTTERANCE_ID="1089-134686-0000"

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

export LIVE_PHONEME_VERIFY_ROOT="${VERIFY_ROOT}"
export LIBRISPEECH_TEST_CLEAN_ROOT="${DATASET_ROOT}"
export VOICE2VOCALSYNTH_BUILD_DIR="${BUILD_DIR}"

output="$("${SCRIPT_DIR}/play_librispeech_clips_linux.sh" \
  --dry-run \
  --utterance-id "${UTTERANCE_ID}" \
  --playback-device "FixtureSink" \
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
assert plan["playbackDevice"] == "FixtureSink"
assert len(plan["clips"]) == 1
clip = plan["clips"][0]
assert clip["utteranceId"] == utterance_id
assert clip["durationSeconds"] > 0.0
assert clip["startOffsetSeconds"] == 0.0
PY

if ! printf '%s\n' "${output}" | grep -q "Dry run complete:"; then
  echo "error: play script did not report dry-run completion" >&2
  printf '%s\n' "${output}" >&2
  exit 1
fi

echo "ScriptDryRunTests passed"
