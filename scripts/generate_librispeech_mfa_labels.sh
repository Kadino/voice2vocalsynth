#!/usr/bin/env bash
# Generate LibriSpeech test-clean MFA reference labels (checklist item 4).
#
# MFA is not bundled. This script documents install/model steps and fails early
# when MFA, ffmpeg, or required pretrained models are missing.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

VERIFY_ROOT="${LIVE_PHONEME_VERIFY_ROOT:-${HOME}/.local/share/Voice2VocalSynth/LivePhonemeVerify}"
DATASET_ROOT="${LIBRISPEECH_TEST_CLEAN_ROOT:-${VERIFY_ROOT}/datasets/LibriSpeech/test-clean}"
LABELS_ROOT="${VERIFY_ROOT}/labels/librispeech-test-clean"
SETUP_BIN="${REPO_ROOT}/build/Voice2VocalSynthLibriSpeechSetup"
CONVERT_BIN="${REPO_ROOT}/build/Voice2VocalSynthMfaLabelConvert"

ACOUSTIC_MODEL="${MFA_ACOUSTIC_MODEL:-english_us_arpa}"
DICTIONARY_MODEL="${MFA_DICTIONARY_MODEL:-english_us_arpa}"
DEFAULT_SUBSET=20

usage() {
  cat <<EOF
Usage: $(basename "$0") [--help] [--check-mfa] [--subset N] [--all]

Generate per-utterance ARPABET reference labels from LibriSpeech test-clean using
Montreal Forced Aligner (MFA). Output JSON files are accepted by
loadPhonemeFrameLabelsJson.

Environment:
  LIVE_PHONEME_VERIFY_ROOT    Verification data root
  LIBRISPEECH_TEST_CLEAN_ROOT Optional dataset override
  MFA_ACOUSTIC_MODEL          Default: english_us_arpa
  MFA_DICTIONARY_MODEL        Default: english_us_arpa

Commands:
  --help       Show MFA install/model instructions and exit
  --check-mfa  Verify MFA, ffmpeg, and required models are available
  --subset N   Align first N utterances (default: ${DEFAULT_SUBSET})
  --all        Align the full test-clean corpus

MFA install (not bundled):
  https://montreal-forced-aligner.readthedocs.io/en/latest/getting_started.html

  Typical conda setup:
    conda create -n mfa -c conda-forge montreal-forced-aligner
    conda activate mfa
    mfa version

Required pretrained models (download once):
    mfa model download acoustic ${ACOUSTIC_MODEL}
    mfa model download dictionary ${DICTIONARY_MODEL}

Other host dependencies:
  - ffmpeg (FLAC -> 16 kHz mono WAV corpus prep)

Output:
  \${LIVE_PHONEME_VERIFY_ROOT}/labels/librispeech-test-clean/<utterance-id>.json
  \${LIVE_PHONEME_VERIFY_ROOT}/labels/librispeech-test-clean/manifest.json

Notes:
  - MFA stress digits are stripped in conversion (AE0 -> AE).
  - Alignment parameters and dictionary/G2P tuning are expected to be refined later.
EOF
}

require_build_targets() {
  if [[ ! -x "${SETUP_BIN}" || ! -x "${CONVERT_BIN}" ]]; then
    mkdir -p "${REPO_ROOT}/build"
    CXX="${CXX:-g++}" cmake -S "${REPO_ROOT}" -B "${REPO_ROOT}/build" -DVOICE2VOCALSYNTH_BUILD_JUCE_APP=OFF >&2
    cmake --build "${REPO_ROOT}/build" --target Voice2VocalSynthLibriSpeechSetup Voice2VocalSynthMfaLabelConvert -j"$(nproc)" >&2
  fi
}

check_mfa() {
  if ! command -v mfa >/dev/null 2>&1; then
    echo "error: MFA is not installed. Run with --help for install instructions." >&2
    exit 1
  fi
  if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "error: ffmpeg is required to convert FLAC clips to WAV for MFA." >&2
    exit 1
  fi

  local mfa_version
  mfa_version="$(mfa version 2>/dev/null | tr '\n' ' ' | sed 's/[[:space:]]*$//')"
  if [[ -z "${mfa_version}" ]]; then
    echo "error: unable to read MFA version from 'mfa version'." >&2
    exit 1
  fi

  if ! mfa model inspect acoustic "${ACOUSTIC_MODEL}" >/dev/null 2>&1; then
    echo "error: MFA acoustic model '${ACOUSTIC_MODEL}' is not installed." >&2
    echo "Run: mfa model download acoustic ${ACOUSTIC_MODEL}" >&2
    exit 1
  fi
  if ! mfa model inspect dictionary "${DICTIONARY_MODEL}" >/dev/null 2>&1; then
    echo "error: MFA dictionary '${DICTIONARY_MODEL}' is not installed." >&2
    echo "Run: mfa model download dictionary ${DICTIONARY_MODEL}" >&2
    exit 1
  fi

  echo "MFA ready: ${mfa_version}"
  echo "acoustic model: ${ACOUSTIC_MODEL}"
  echo "dictionary: ${DICTIONARY_MODEL}"
}

prepare_and_align() {
  local limit="$1"
  local subset_mode="$2"

  require_build_targets
  check_mfa

  if [[ ! -d "${DATASET_ROOT}" ]]; then
    echo "error: LibriSpeech dataset not found at ${DATASET_ROOT}" >&2
    echo "Run scripts/setup_librispeech_test_clean.sh --download first." >&2
    exit 1
  fi

  local work_root
  work_root="$(mktemp -d "${TMPDIR:-/tmp}/v2vs-mfa-labels.XXXXXX")"
  local corpus_dir="${work_root}/corpus"
  local textgrid_dir="${work_root}/textgrids"
  mkdir -p "${corpus_dir}" "${textgrid_dir}" "${LABELS_ROOT}"

  mapfile -t utterance_rows < <("${SETUP_BIN}" --list-utterances --dataset-root "${DATASET_ROOT}" --limit "${limit}")
  if [[ "${#utterance_rows[@]}" -eq 0 ]]; then
    echo "error: no utterances found under ${DATASET_ROOT}" >&2
    exit 1
  fi

  local id flac transcript
  for row in "${utterance_rows[@]}"; do
    [[ -z "${row}" ]] && continue
    IFS=$'\t' read -r id flac transcript <<<"${row}"
    local wav="${corpus_dir}/${id}.wav"
    local lab="${corpus_dir}/${id}.lab"
    ffmpeg -nostdin -loglevel error -y -i "${flac}" -ar 16000 -ac 1 "${wav}"
    printf '%s\n' "${transcript,,}" > "${lab}"
  done

  echo "Aligning ${#utterance_rows[@]} utterances with MFA..." >&2
  mfa align --clean --single_speaker "${corpus_dir}" "${DICTIONARY_MODEL}" "${ACOUSTIC_MODEL}" "${textgrid_dir}"

  "${CONVERT_BIN}" --convert-textgrids --textgrid-root "${textgrid_dir}" --labels-root "${LABELS_ROOT}" --verify-root "${VERIFY_ROOT}"

  local mfa_version label_count
  mfa_version="$(mfa version 2>/dev/null | tr '\n' ' ' | sed 's/[[:space:]]*$//')"
  label_count="$(find "${LABELS_ROOT}" -maxdepth 1 -name '*.json' ! -name 'manifest.json' | wc -l | tr -d ' ')"
  "${CONVERT_BIN}" --write-manifest \
    --verify-root "${VERIFY_ROOT}" \
    --dataset-root "${DATASET_ROOT}" \
    --mfa-version "${mfa_version}" \
    --subset-mode "${subset_mode}" \
    --utterance-count "${#utterance_rows[@]}" \
    --label-file-count "${label_count}"

  rm -rf "${work_root}"
  echo "Wrote ${label_count} label files to ${LABELS_ROOT}" >&2
}

ACTION=""
SUBSET_COUNT="${DEFAULT_SUBSET}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    --check-mfa)
      ACTION="check"
      shift
      ;;
    --subset)
      ACTION="subset"
      SUBSET_COUNT="${2:?missing value for --subset}"
      shift 2
      ;;
    --all)
      ACTION="all"
      shift
      ;;
    *)
      echo "error: unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

case "${ACTION:-subset}" in
  check)
    check_mfa
    ;;
  subset)
    prepare_and_align "${SUBSET_COUNT}" "subset"
    ;;
  all)
    prepare_and_align 0 "all"
    ;;
esac
