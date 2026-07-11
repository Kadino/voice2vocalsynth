#!/usr/bin/env bash
# Validate Linux virtual-audio routing for live phoneme verification (checklist item 5).
#
# PipeWire/Pulse loopback is the primary route. ALSA snd-aloop and JACK are documented
# alternates. This script documents host setup and fails early when routing is missing.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

VERIFY_ROOT="${LIVE_PHONEME_VERIFY_ROOT:-${HOME}/.local/share/Voice2VocalSynth/LivePhonemeVerify}"
VALIDATE_BIN="${REPO_ROOT}/build/Voice2VocalSynthLinuxAudioValidate"
SINK_NAME="${LINUX_AUDIO_SINK_NAME:-LivePhonemeVerify}"
MONITOR_SOURCE="${SINK_NAME}.monitor"
DEFAULT_ROUTE="auto"
PROBE=0
WRITE_MANIFEST=0

usage() {
  cat <<EOF
Usage: $(basename "$0") [--help] [--check] [--probe] [--write-manifest] [--route auto|pipewire|alsa|jack]

Validate Linux virtual-audio routing for the live JUCE shell verification workflow.

Environment:
  LIVE_PHONEME_VERIFY_ROOT   Verification data root
  LINUX_AUDIO_SINK_NAME      Recommended PipeWire null sink (default: LivePhonemeVerify)

Primary route (recommended):
  PipeWire/Pulse null sink + monitor source loopback

  1. Ensure PipeWire or PulseAudio is running.
  2. Create the verification sink once:
       pactl load-module module-null-sink sink_name=${SINK_NAME} \\
         sink_properties=device.description=${SINK_NAME}
  3. Route playback to sink: ${SINK_NAME}
  4. Select JUCE shell input: ${MONITOR_SOURCE}

Alternate routes:
  ALSA snd-aloop:
    sudo modprobe snd-aloop
    # Playback: plughw:Loopback,0,0  Capture: plughw:Loopback,1,0

  JACK (including PipeWire's pw-jack):
    Start a JACK server and connect playback output ports to the shell input ports.
    This script only verifies that jack_lsp can talk to a running server.

Commands:
  --help            Show setup instructions and exit
  --check           Validate routing and print the selected route (default)
  --probe           After --check, play a short test tone and verify capture energy
  --write-manifest  Write ~/.local/share/.../linux-virtual-audio.json on success
  --route <name>    Route selection: auto (default), pipewire, alsa, jack

Output:
  On success, prints a JSON check report to stdout and exits 0.
  On failure, prints an error to stderr and exits 1.
EOF
}

require_build_target() {
  if [[ ! -x "${VALIDATE_BIN}" ]]; then
    mkdir -p "${REPO_ROOT}/build"
    CXX="${CXX:-g++}" cmake -S "${REPO_ROOT}" -B "${REPO_ROOT}/build" -DVOICE2VOCALSYNTH_BUILD_JUCE_APP=OFF >&2
    cmake --build "${REPO_ROOT}/build" --target Voice2VocalSynthLinuxAudioValidate -j"$(nproc)" >&2
  fi
}

json_escape() {
  python3 - <<'PY' "$1"
import json, sys
print(json.dumps(sys.argv[1]))
PY
}

emit_report() {
  local valid="$1"
  local route="$2"
  local playback="$3"
  local capture="$4"
  local server="$5"
  local note="$6"
  local probe_passed="${7:-false}"
  local error="${8:-}"

  printf '{'
  printf '"valid":%s,' "$( [[ "${valid}" == "1" ]] && echo true || echo false )"
  printf '"route":%s,' "$(json_escape "${route}")"
  printf '"playbackDevice":%s,' "$(json_escape "${playback}")"
  printf '"captureDevice":%s,' "$(json_escape "${capture}")"
  printf '"serverName":%s,' "$(json_escape "${server}")"
  printf '"note":%s,' "$(json_escape "${note}")"
  printf '"probePassed":%s' "$( [[ "${probe_passed}" == "1" ]] && echo true || echo false )"
  if [[ -n "${error}" ]]; then
    printf ',"error":%s' "$(json_escape "${error}")"
  fi
  printf '}\n'
}

fail_report() {
  local route="$1"
  local error="$2"
  emit_report 0 "${route}" "" "" "" "" 0 "${error}" >&2
  echo "error: ${error}" >&2
  exit 1
}

check_pipewire() {
  if ! command -v pactl >/dev/null 2>&1; then
    return 1
  fi
  if ! pactl info >/dev/null 2>&1; then
    return 1
  fi

  local server_name sinks sources
  server_name="$(pactl info | awk -F': ' '/^Server Name:/ {print $2; exit}')"
  sinks="$(pactl list sinks)"
  sources="$(pactl list sources)"

  if ! grep -Fq "Name: ${SINK_NAME}" <<<"${sinks}"; then
    return 1
  fi
  if ! grep -Fq "Name: ${MONITOR_SOURCE}" <<<"${sources}"; then
    return 1
  fi

  SELECTED_ROUTE="pipewire-loopback"
  PLAYBACK_DEVICE="${SINK_NAME}"
  CAPTURE_DEVICE="${MONITOR_SOURCE}"
  SERVER_NAME="${server_name}"
  ROUTE_NOTE="Playback to ${SINK_NAME} is captured from ${MONITOR_SOURCE} for the JUCE shell input."
  return 0
}

check_alsa() {
  if ! command -v aplay >/dev/null 2>&1 || ! command -v arecord >/dev/null 2>&1; then
    return 1
  fi
  local aplay_list arecord_list
  aplay_list="$(aplay -l 2>/dev/null || true)"
  arecord_list="$(arecord -l 2>/dev/null || true)"
  if ! grep -Eiq 'loopback' <<<"${aplay_list}" || ! grep -Eiq 'loopback' <<<"${arecord_list}"; then
    if [[ "${ROUTE_MODE}" == "alsa" ]]; then
      fail_report "alsa-snd-aloop" "ALSA Loopback device not found. Run: sudo modprobe snd-aloop"
    fi
    return 1
  fi

  SELECTED_ROUTE="alsa-snd-aloop"
  PLAYBACK_DEVICE="plughw:Loopback,0,0"
  CAPTURE_DEVICE="plughw:Loopback,1,0"
  SERVER_NAME="ALSA snd-aloop"
  ROUTE_NOTE="Route playback to loopback playback subdevice and capture from loopback capture subdevice."
}

check_jack() {
  if ! command -v jack_lsp >/dev/null 2>&1; then
    return 1
  fi
  if ! jack_lsp >/dev/null 2>&1; then
    if [[ "${ROUTE_MODE}" == "jack" ]]; then
      fail_report "jack" "JACK server is not reachable via jack_lsp"
    fi
    return 1
  fi

  SELECTED_ROUTE="jack"
  PLAYBACK_DEVICE="jack-playback"
  CAPTURE_DEVICE="jack-capture"
  SERVER_NAME="JACK"
  ROUTE_NOTE="Connect playback output ports to the JUCE shell input ports with jack_connect or qjackctl."
}

select_route() {
  case "${ROUTE_MODE}" in
    pipewire)
      if ! check_pipewire; then
        if command -v pactl >/dev/null 2>&1 && pactl info >/dev/null 2>&1; then
          fail_report "pipewire-loopback" \
            "Missing null sink '${SINK_NAME}'. Run: pactl load-module module-null-sink sink_name=${SINK_NAME} sink_properties=device.description=${SINK_NAME}"
        fi
        fail_report "pipewire-loopback" "PipeWire/PulseAudio is not available via pactl"
      fi
      ;;
    alsa)
      if ! check_alsa; then
        fail_report "alsa-snd-aloop" "ALSA Loopback device not found. Run: sudo modprobe snd-aloop"
      fi
      ;;
    jack)
      if ! check_jack; then
        fail_report "jack" "JACK server is not reachable via jack_lsp"
      fi
      ;;
    auto)
      if command -v pactl >/dev/null 2>&1; then
        if check_pipewire; then
          :
        else
          fail_report "pipewire-loopback" \
            "PipeWire/Pulse is present but the recommended null sink '${SINK_NAME}' is missing. Run: pactl load-module module-null-sink sink_name=${SINK_NAME} sink_properties=device.description=${SINK_NAME}"
        fi
      elif check_alsa; then
        :
      elif check_jack; then
        :
      else
        fail_report "pipewire-loopback" \
          "No supported Linux virtual-audio route found. Install PipeWire/Pulse with pactl, create sink '${SINK_NAME}', or configure ALSA snd-aloop / JACK. Run with --help for setup steps."
      fi
      ;;
    *)
      fail_report "unknown" "Unsupported route mode: ${ROUTE_MODE}"
      ;;
  esac
}

probe_route() {
  if [[ "${SELECTED_ROUTE}" != "pipewire-loopback" ]]; then
    echo "warning: --probe is only implemented for pipewire-loopback; skipping probe" >&2
    PROBE_PASSED=0
    return 0
  fi
  if ! command -v ffmpeg >/dev/null 2>&1; then
    fail_report "${SELECTED_ROUTE}" "--probe requires ffmpeg"
  fi
  if ! command -v parec >/dev/null 2>&1; then
    fail_report "${SELECTED_ROUTE}" "--probe requires parec (pulseaudio-utils)"
  fi

  local work_dir capture_file
  work_dir="$(mktemp -d "${TMPDIR:-/tmp}/v2vs-audio-probe.XXXXXX")"
  capture_file="${work_dir}/capture.raw"
  parec --device="${CAPTURE_DEVICE}" --format=s16le --rate=16000 --channels=1 -d 1 > "${capture_file}" &
  local parec_pid=$!
  sleep 0.2
  ffmpeg -nostdin -loglevel error -f lavfi -i sine=frequency=440:duration=0.5 -f pulse -device "${PLAYBACK_DEVICE}" -
  wait "${parec_pid}" || true

  local byte_count
  byte_count="$(wc -c < "${capture_file}" | tr -d ' ')"
  rm -rf "${work_dir}"
  if [[ "${byte_count}" -lt 3200 ]]; then
    fail_report "${SELECTED_ROUTE}" "Audio probe did not observe sufficient energy on ${CAPTURE_DEVICE}"
  fi
  PROBE_PASSED=1
}

ACTION="check"
ROUTE_MODE="${DEFAULT_ROUTE}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    --check)
      ACTION="check"
      shift
      ;;
    --probe)
      PROBE=1
      shift
      ;;
    --write-manifest)
      WRITE_MANIFEST=1
      shift
      ;;
    --route)
      ROUTE_MODE="${2:?missing value for --route}"
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
  fail_report "unsupported" "Linux virtual-audio validation is only supported on Linux"
fi

select_route

PROBE_PASSED=0
if [[ "${PROBE}" -eq 1 ]]; then
  probe_route
fi

report="$(emit_report 1 "${SELECTED_ROUTE}" "${PLAYBACK_DEVICE}" "${CAPTURE_DEVICE}" "${SERVER_NAME}" "${ROUTE_NOTE}" "${PROBE_PASSED}")"
printf '%s\n' "${report}"

echo "Linux virtual audio validation passed" >&2
echo "route: ${SELECTED_ROUTE}" >&2
echo "playback: ${PLAYBACK_DEVICE}" >&2
echo "capture: ${CAPTURE_DEVICE}" >&2
echo "server: ${SERVER_NAME}" >&2

if [[ "${WRITE_MANIFEST}" -eq 1 ]]; then
  require_build_target
  report_file="$(mktemp)"
  printf '%s\n' "${report}" > "${report_file}"
  "${VALIDATE_BIN}" --write-manifest --verify-root "${VERIFY_ROOT}" --report "${report_file}"
  rm -f "${report_file}"
fi

exit 0
