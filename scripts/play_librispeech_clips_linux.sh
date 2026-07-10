#!/usr/bin/env bash
# Play LibriSpeech test-clean clips at 1.0x real-time into Linux virtual audio (item 6).
#
# Requires a prior successful validate_linux_virtual_audio.sh --write-manifest run unless
# --playback-device is provided explicitly.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

VERIFY_ROOT="${LIVE_PHONEME_VERIFY_ROOT:-${HOME}/.local/share/Voice2VocalSynth/LivePhonemeVerify}"
DATASET_ROOT="${LIBRISPEECH_TEST_CLEAN_ROOT:-${VERIFY_ROOT}/datasets/LibriSpeech/test-clean}"
AUDIO_MANIFEST="${VERIFY_ROOT}/linux-virtual-audio.json"
SETUP_BIN="${REPO_ROOT}/build/Voice2VocalSynthLibriSpeechSetup"
PLAYBACK_BIN="${REPO_ROOT}/build/Voice2VocalSynthLibriSpeechPlayback"

DEFAULT_SUBSET=20
DEFAULT_GAP_SECONDS=0.5
ACTION="play"
SUBSET_COUNT="${DEFAULT_SUBSET}"
GAP_SECONDS="${DEFAULT_GAP_SECONDS}"
PLAYBACK_DEVICE_OVERRIDE=""
UTTERANCE_ID=""
RUN_DIR=""

usage() {
  cat <<EOF
Usage: $(basename "$0") [--help] [--dry-run] [--subset N] [--all] [--utterance-id ID] \\
       [--gap-seconds S] [--playback-device DEV] [--run-dir DIR]

Play LibriSpeech test-clean FLAC clips at 1.0x real-time into the Linux virtual-audio
route validated by scripts/validate_linux_virtual_audio.sh.

Environment:
  LIVE_PHONEME_VERIFY_ROOT      Verification data root
  LIBRISPEECH_TEST_CLEAN_ROOT   Optional dataset override

Commands:
  --help              Show this help and exit
  --dry-run           Build playback-manifest.json only (no audio output)
  --subset N          Play first N utterances (default: ${DEFAULT_SUBSET})
  --all               Play the full test-clean corpus
  --utterance-id ID   Play one utterance
  --gap-seconds S     Silence between clips (default: ${DEFAULT_GAP_SECONDS})
  --playback-device   Override playback device from linux-virtual-audio.json
  --run-dir DIR       Write manifest under DIR (default: new timestamped run directory)

Dependencies:
  - ffmpeg and ffprobe
  - Voice2VocalSynthLibriSpeechPlayback
  - linux-virtual-audio.json from validate_linux_virtual_audio.sh --write-manifest

Output:
  <run-dir>/playback-manifest.json with scheduled start offsets and clip durations
EOF
}

require_build_targets() {
  if [[ ! -x "${SETUP_BIN}" || ! -x "${PLAYBACK_BIN}" ]]; then
    mkdir -p "${REPO_ROOT}/build"
    CXX="${CXX:-g++}" cmake -S "${REPO_ROOT}" -B "${REPO_ROOT}/build" -DVOICE2VOCALSYNTH_BUILD_JUCE_APP=OFF >&2
    cmake --build "${REPO_ROOT}/build" --target Voice2VocalSynthLibriSpeechSetup Voice2VocalSynthLibriSpeechPlayback -j"$(nproc)" >&2
  fi
}

require_tools() {
  if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "error: ffmpeg is required for real-time playback" >&2
    exit 1
  fi
  if ! command -v ffprobe >/dev/null 2>&1; then
    echo "error: ffprobe is required to measure clip durations" >&2
    exit 1
  fi
  if ! command -v python3 >/dev/null 2>&1; then
    echo "error: python3 is required to read playback manifests" >&2
    exit 1
  fi
}

load_audio_route() {
  if [[ -n "${PLAYBACK_DEVICE_OVERRIDE}" ]]; then
    if [[ -f "${AUDIO_MANIFEST}" ]]; then
      ROUTE_ID="$(python3 - <<'PY' "${AUDIO_MANIFEST}"
import json, sys
print(json.load(open(sys.argv[1])).get("selectedRoute", "pipewire-loopback"))
PY
)"
    else
      ROUTE_ID="pipewire-loopback"
    fi
    PLAYBACK_DEVICE="${PLAYBACK_DEVICE_OVERRIDE}"
    return 0
  fi

  if [[ ! -f "${AUDIO_MANIFEST}" ]]; then
    echo "error: missing ${AUDIO_MANIFEST}" >&2
    echo "Run scripts/validate_linux_virtual_audio.sh --write-manifest first." >&2
    exit 1
  fi

  ROUTE_ID="$(python3 - <<'PY' "${AUDIO_MANIFEST}"
import json, sys
data = json.load(open(sys.argv[1]))
print(data["selectedRoute"])
PY
)"
  PLAYBACK_DEVICE="$(python3 - <<'PY' "${AUDIO_MANIFEST}"
import json, sys
data = json.load(open(sys.argv[1]))
print(data["playbackDevice"])
PY
)"
}

list_utterance_rows() {
  local limit="$1"
  local -a args=(--list-utterances --dataset-root "${DATASET_ROOT}")
  if [[ "${limit}" -gt 0 ]]; then
    args+=(--limit "${limit}")
  fi
  "${SETUP_BIN}" "${args[@]}"
}

filter_utterance_rows() {
  local target_id="$1"
  list_utterance_rows 0 | awk -F '\t' -v id="${target_id}" '$1 == id { print; found=1 } END { exit(found ? 0 : 1) }'
}

build_durations_tsv() {
  local rows_file="$1"
  local durations_file="$2"
  : > "${durations_file}"
  local id flac transcript duration
  while IFS= read -r row; do
    [[ -z "${row}" ]] && continue
    IFS=$'\t' read -r id flac transcript <<<"${row}"
    duration="$(ffprobe -v error -show_entries format=duration -of csv=p=0 "${flac}")"
    if [[ -z "${duration}" ]]; then
      echo "error: unable to read duration for ${id}" >&2
      exit 1
    fi
    printf '%s\t%s\n' "${id}" "${duration}" >> "${durations_file}"
  done < "${rows_file}"
}

play_manifest() {
  local manifest_path="$1"
  python3 - <<'PY' "${manifest_path}" "${GAP_SECONDS}"
import json, subprocess, sys, time

manifest_path, gap_seconds = sys.argv[1], float(sys.argv[2])
plan = json.load(open(manifest_path))
route_id = plan["routeId"]
device = plan["playbackDevice"]

def output_args():
    if route_id == "pipewire-loopback":
        return ["-f", "pulse", "-device", device]
    if route_id == "alsa-snd-aloop":
        return ["-f", "alsa", "-device", device]
    raise SystemExit(f"unsupported route id: {route_id}")

clips = plan["clips"]
for index, clip in enumerate(clips):
  flac = clip["flacPath"]
  cmd = ["ffmpeg", "-nostdin", "-loglevel", "error", "-re", "-i", flac, *output_args(), "-"]
  subprocess.run(cmd, check=True)
  if index + 1 < len(clips):
    time.sleep(gap_seconds)
PY
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    --dry-run)
      ACTION="dry-run"
      shift
      ;;
    --subset)
      SUBSET_COUNT="${2:?missing value for --subset}"
      shift 2
      ;;
    --all)
      SUBSET_COUNT=0
      shift
      ;;
    --utterance-id)
      UTTERANCE_ID="${2:?missing value for --utterance-id}"
      shift 2
      ;;
    --gap-seconds)
      GAP_SECONDS="${2:?missing value for --gap-seconds}"
      shift 2
      ;;
    --playback-device)
      PLAYBACK_DEVICE_OVERRIDE="${2:?missing value for --playback-device}"
      shift 2
      ;;
    --run-dir)
      RUN_DIR="${2:?missing value for --run-dir}"
      shift 2
      ;;
    *)
      echo "error: unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "error: LibriSpeech Linux playback is only supported on Linux" >&2
  exit 1
fi

require_tools
require_build_targets
load_audio_route

if [[ ! -d "${DATASET_ROOT}" ]]; then
  echo "error: LibriSpeech dataset not found at ${DATASET_ROOT}" >&2
  echo "Run scripts/setup_librispeech_test_clean.sh --download first." >&2
  exit 1
fi

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/v2vs-playback.XXXXXX")"
rows_file="${work_dir}/utterances.tsv"
durations_file="${work_dir}/durations.tsv"

if [[ -n "${UTTERANCE_ID}" ]]; then
  if ! filter_utterance_rows "${UTTERANCE_ID}" > "${rows_file}"; then
    echo "error: utterance not found: ${UTTERANCE_ID}" >&2
    exit 1
  fi
else
  list_utterance_rows "${SUBSET_COUNT}" > "${rows_file}"
fi

if [[ ! -s "${rows_file}" ]]; then
  echo "error: no utterances selected for playback" >&2
  exit 1
fi

build_durations_tsv "${rows_file}" "${durations_file}"

manifest_args=(
  --build-manifest
  --verify-root "${VERIFY_ROOT}"
  --dataset-root "${DATASET_ROOT}"
  --durations-tsv "${durations_file}"
  --gap-seconds "${GAP_SECONDS}"
)
if [[ -n "${RUN_DIR}" ]]; then
  manifest_args+=(--run-dir "${RUN_DIR}")
fi
if [[ -n "${UTTERANCE_ID}" ]]; then
  manifest_args+=(--utterance-id "${UTTERANCE_ID}")
elif [[ "${SUBSET_COUNT}" -eq 0 ]]; then
  manifest_args+=(--all)
else
  manifest_args+=(--subset "${SUBSET_COUNT}")
fi
if [[ -n "${PLAYBACK_DEVICE_OVERRIDE}" ]]; then
  manifest_args+=(--playback-device "${PLAYBACK_DEVICE_OVERRIDE}")
fi

build_output="$("${PLAYBACK_BIN}" "${manifest_args[@]}")"
printf '%s\n' "${build_output}" >&2
manifest_path="$(printf '%s\n' "${build_output}" | awk -F': ' '/^Wrote playback manifest:/ {print $2}')"

if [[ "${ACTION}" == "dry-run" ]]; then
  echo "Dry run complete: ${manifest_path}" >&2
  rm -rf "${work_dir}"
  exit 0
fi

echo "Playing $(wc -l < "${rows_file}" | tr -d ' ') clips at 1.0x to ${PLAYBACK_DEVICE} (${ROUTE_ID})" >&2
play_manifest "${manifest_path}"
rm -rf "${work_dir}"
echo "Playback complete" >&2
