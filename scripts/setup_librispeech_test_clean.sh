#!/usr/bin/env bash
# Setup and validate LibriSpeech test-clean for live phoneme verification.
#
# This script handles dataset discovery/download only. MFA label generation is a
# separate follow-up step (see docs/live-phoneme-verification-plan.md).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

VERIFY_ROOT="${LIVE_PHONEME_VERIFY_ROOT:-${HOME}/.local/share/Voice2VocalSynth/LivePhonemeVerify}"
DATASETS_DIR="${VERIFY_ROOT}/datasets"
ARCHIVE_URL="https://www.openslr.org/resources/12/test-clean.tar.gz"
ARCHIVE_NAME="test-clean.tar.gz"
SETUP_BIN="${REPO_ROOT}/build/Voice2VocalSynthLibriSpeechSetup"

usage() {
  cat <<EOF
Usage: $(basename "$0") [--help] [--download] [--verify] [--manifest]

Prepare LibriSpeech test-clean for live phoneme verification (checklist item 3).

Environment:
  LIVE_PHONEME_VERIFY_ROOT   Verification data root
                             (default: ~/.local/share/Voice2VocalSynth/LivePhonemeVerify)
  LIBRISPEECH_TEST_CLEAN_ROOT Optional override for an existing extracted dataset root

Default dataset layout after download:
  \${LIVE_PHONEME_VERIFY_ROOT}/datasets/LibriSpeech/test-clean/

Commands:
  --help       Show download instructions and exit
  --download   Download and extract test-clean.tar.gz into the datasets directory
  --verify     Validate an existing dataset (builds setup tool if needed)
  --manifest   Validate and write datasets/LibriSpeech/librispeech-test-clean-manifest.json

Manual download (if --download is unavailable):
  mkdir -p "${DATASETS_DIR}"
  cd "${DATASETS_DIR}"
  curl -fL -O "${ARCHIVE_URL}"
  tar -xzf "${ARCHIVE_NAME}"
  # Extracted layout: ${DATASETS_DIR}/LibriSpeech/test-clean/

Next step (not bundled here):
  Montreal Forced Aligner (MFA) for reference labels — install separately:
    https://montreal-forced-aligner.readthedocs.io/en/latest/getting_started.html

  Typical host setup (conda example):
    conda create -n mfa -c conda-forge montreal-forced-aligner
    conda activate mfa
    mfa version

  Alignment tuning (dictionary/G2P/normalization) is expected to be refined later.
EOF
}

require_setup_binary() {
  if [[ ! -x "${SETUP_BIN}" ]]; then
    echo "Building Voice2VocalSynthLibriSpeechSetup..." >&2
    mkdir -p "${REPO_ROOT}/build"
  fi
  if [[ ! -x "${SETUP_BIN}" ]]; then
    CXX="${CXX:-g++}" cmake -S "${REPO_ROOT}" -B "${REPO_ROOT}/build" -DVOICE2VOCALSYNTH_BUILD_JUCE_APP=OFF >&2
    cmake --build "${REPO_ROOT}/build" --target Voice2VocalSynthLibriSpeechSetup -j"$(nproc)" >&2
  fi
}

do_download() {
  mkdir -p "${DATASETS_DIR}"
  local archive_path="${DATASETS_DIR}/${ARCHIVE_NAME}"
  echo "Downloading ${ARCHIVE_URL}" >&2
  if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 -o "${archive_path}" "${ARCHIVE_URL}"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "${archive_path}" "${ARCHIVE_URL}"
  else
    echo "error: install curl or wget to download LibriSpeech test-clean" >&2
    exit 1
  fi
  echo "Extracting ${archive_path} into ${DATASETS_DIR}" >&2
  tar -xzf "${archive_path}" -C "${DATASETS_DIR}"
  echo "Download complete." >&2
}

ACTION="verify"
if [[ $# -eq 0 ]]; then
  ACTION="verify"
fi

while [[ $# -gt 0 ]]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    --download)
      ACTION="download"
      shift
      ;;
    --verify)
      ACTION="verify"
      shift
      ;;
    --manifest)
      ACTION="manifest"
      shift
      ;;
    *)
      echo "error: unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

case "${ACTION}" in
  download)
    do_download
    require_setup_binary
    "${SETUP_BIN}" --verify --verify-root "${VERIFY_ROOT}"
    "${SETUP_BIN}" --write-manifest --verify-root "${VERIFY_ROOT}"
    ;;
  verify)
    require_setup_binary
    "${SETUP_BIN}" --verify --verify-root "${VERIFY_ROOT}"
    ;;
  manifest)
    require_setup_binary
    "${SETUP_BIN}" --write-manifest --verify-root "${VERIFY_ROOT}"
    ;;
esac
