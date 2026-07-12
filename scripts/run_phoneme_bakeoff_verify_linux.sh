#!/usr/bin/env bash
# Validate the bakeoff matrix and run automatic offline/live-dry-run verification.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${VOICE2VOCALSYNTH_BUILD_DIR:-${REPO_ROOT}/build}"
MATRIX_PATH="${REPO_ROOT}/config/phoneme_bakeoff_matrix.json"
VERIFY_ROOT="${LIVE_PHONEME_VERIFY_ROOT:-${HOME}/.local/share/Voice2VocalSynth/LivePhonemeVerify}"

VALIDATE_ONLY=0
DRY_RUN=0
RUN_OFFLINE=0
RUN_LIVE_DRY_RUN=0
OUTPUT=""
SKIP_BUILD=0
SUBSET=1

usage() {
  cat <<EOF
Usage: $(basename "$0") [--validate-only | --dry-run | --offline | --live-dry-run]
       [--output report.json] [--subset N] [--skip-build]

Reads config/phoneme_bakeoff_matrix.json and verifies integrated backends.

Modes (combinable except --validate-only):
  --validate-only   Schema-check the matrix JSON and exit
  --dry-run         Write bakeoff-verify-plan.json without executing backends
  --offline         Run Voice2VocalSynthPhonemeBakeoff for integrated offline backends
  --live-dry-run    Run compare_live_phoneme_backends_linux.sh --dry-run for live backends

When no mode flag is given, --dry-run is assumed.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --help|-h) usage; exit 0 ;;
    --validate-only) VALIDATE_ONLY=1; shift ;;
    --dry-run) DRY_RUN=1; shift ;;
    --offline) RUN_OFFLINE=1; shift ;;
    --live-dry-run) RUN_LIVE_DRY_RUN=1; shift ;;
    --output) OUTPUT="${2:?missing value for --output}"; shift 2 ;;
    --subset) SUBSET="${2:?missing value for --subset}"; shift 2 ;;
    --skip-build) SKIP_BUILD=1; shift ;;
    *) echo "error: unknown argument: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [[ "${VALIDATE_ONLY}" -eq 0 && "${DRY_RUN}" -eq 0 && "${RUN_OFFLINE}" -eq 0 && "${RUN_LIVE_DRY_RUN}" -eq 0 ]]; then
  DRY_RUN=1
fi

if [[ ! -f "${MATRIX_PATH}" ]]; then
  echo "error: matrix not found: ${MATRIX_PATH}" >&2
  exit 1
fi

python3 "${SCRIPT_DIR}/validate_phoneme_bakeoff_matrix.py" "${MATRIX_PATH}"

if [[ "${VALIDATE_ONLY}" -eq 1 ]]; then
  exit 0
fi

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "error: automatic bakeoff verification currently supports Linux only" >&2
  exit 1
fi

run_dir="${VERIFY_ROOT}/runs/$(date -u +%Y%m%dT%H%M%SZ)-bakeoff-verify"
mkdir -p "${run_dir}"
if [[ -z "${OUTPUT}" ]]; then
  OUTPUT="${run_dir}/bakeoff-verify-report.json"
fi

matrix_summary="$(python3 - "${MATRIX_PATH}" <<'PY'
import json, sys
data = json.load(open(sys.argv[1], encoding="utf-8"))
integrated = [b for b in data["backends"] if b["status"] == "integrated"]
offline = sorted(
    {
        b["bakeoffName"]
        for b in integrated
        if b["offlineBakeoff"] and not b.get("requiresOnnxModel")
    },
    key=lambda name: next(
        row["comparePriority"] for row in integrated if row["bakeoffName"] == name
    ),
)
offline_with_onnx = sorted(
    {
        b["bakeoffName"]
        for b in integrated
        if b["offlineBakeoff"]
    },
    key=lambda name: next(
        row["comparePriority"] for row in integrated if row["bakeoffName"] == name
    ),
)
live = sorted(
    {b["shellName"] for b in integrated if b["liveVerification"] and b.get("shellName")},
    key=lambda name: next(
        row["comparePriority"] for row in integrated if row.get("shellName") == name
    ),
)
print(json.dumps({
    "offlineBakeoff": offline,
    "offlineBakeoffWithOnnx": offline_with_onnx,
    "liveShell": live,
}))
PY
)"
offline_backends="$(python3 -c 'import json,sys; print(",".join(json.loads(sys.argv[1])["offlineBakeoff"]))' "${matrix_summary}")"
live_backends="$(python3 -c 'import json,sys; print(",".join(json.loads(sys.argv[1])["liveShell"]))' "${matrix_summary}")"

fixture_reference="${REPO_ROOT}/tests/fixtures/phoneme_eval/reference_frames.json"
fixture_audio="${run_dir}/offline_fixture.wav"
fixture_report="${run_dir}/offline-bakeoff.json"

if [[ "${DRY_RUN}" -eq 1 ]]; then
  python3 - "${OUTPUT}" "${run_dir}" "${offline_backends}" "${live_backends}" "${SUBSET}" <<'PY'
import json, pathlib, sys

output = pathlib.Path(sys.argv[1])
run_dir = pathlib.Path(sys.argv[2])
offline = [item for item in sys.argv[3].split(",") if item]
live = [item for item in sys.argv[4].split(",") if item]
subset = int(sys.argv[5])
payload = {
    "schemaVersion": 1,
    "dryRun": True,
    "matrixPath": "config/phoneme_bakeoff_matrix.json",
    "offlineBakeoff": {
        "backends": offline,
        "referencePath": "tests/fixtures/phoneme_eval/reference_frames.json",
        "audioPath": str(run_dir / "offline_fixture.wav"),
        "reportPath": str(run_dir / "offline-bakeoff.json"),
    },
    "liveDryRun": {
        "backends": live,
        "subset": subset,
        "comparisonPlanPath": str(run_dir / "live-comparison-plan.json"),
    },
}
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
print(f"Dry run complete: {output}")
PY
  if [[ "${RUN_OFFLINE}" -eq 0 && "${RUN_LIVE_DRY_RUN}" -eq 0 ]]; then
    exit 0
  fi
fi

offline_status="skipped"
offline_error=""
live_status="skipped"
live_error=""

if [[ "${RUN_OFFLINE}" -eq 1 ]]; then
  if [[ -z "${offline_backends}" ]]; then
    offline_status="skipped"
    offline_error="no integrated offline backends in matrix"
  else
    bakeoff_bin="${BUILD_DIR}/Voice2VocalSynthPhonemeBakeoff"
    if [[ "${SKIP_BUILD}" -eq 0 ]]; then
      cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}"
      cmake --build "${BUILD_DIR}" --target Voice2VocalSynthPhonemeBakeoff -j"$(nproc)"
    fi
    if [[ ! -x "${bakeoff_bin}" ]]; then
      offline_status="failed"
      offline_error="Voice2VocalSynthPhonemeBakeoff binary missing"
    else
      ffmpeg -y -nostdin -loglevel error \
        -f lavfi -i "sine=frequency=220:duration=1.0" \
        -ar 16000 -ac 1 "${fixture_audio}"
      set +e
      "${bakeoff_bin}" \
        --reference "${fixture_reference}" \
        --audio "${fixture_audio}" \
        --backends "${offline_backends}" \
        --out "${fixture_report}" \
        >"${run_dir}/offline-bakeoff.log" 2>&1
      bakeoff_exit=$?
      set -e
      if [[ "${bakeoff_exit}" -eq 0 && -f "${fixture_report}" ]]; then
        offline_status="passed"
      else
        offline_status="failed"
        offline_error="offline bakeoff exited ${bakeoff_exit}"
      fi
    fi
  fi
fi

if [[ "${RUN_LIVE_DRY_RUN}" -eq 1 ]]; then
  if [[ -z "${live_backends}" ]]; then
    live_status="skipped"
    live_error="no integrated live backends in matrix"
  else
    live_plan="${run_dir}/live-comparison-plan.json"
    set +e
    "${SCRIPT_DIR}/compare_live_phoneme_backends_linux.sh" \
      --dry-run \
      --backends "${live_backends}" \
      --subset "${SUBSET}" \
      --output "${live_plan}" \
      >"${run_dir}/live-dry-run.log" 2>&1
    live_exit=$?
    set -e
    if [[ "${live_exit}" -eq 0 && -f "${live_plan}" ]]; then
      live_status="passed"
    else
      live_status="failed"
      live_error="live dry-run exited ${live_exit}"
    fi
  fi
fi

python3 - "${OUTPUT}" "${offline_status}" "${offline_error}" "${live_status}" "${live_error}" \
  "${fixture_report}" "${run_dir}/live-comparison-plan.json" "${offline_backends}" "${live_backends}" <<'PY'
import json, pathlib, sys

output = pathlib.Path(sys.argv[1])
offline_status, offline_error, live_status, live_error = sys.argv[2:6]
offline_report = pathlib.Path(sys.argv[6])
live_plan = pathlib.Path(sys.argv[7])
offline_backends = [item for item in sys.argv[8].split(",") if item]
live_backends = [item for item in sys.argv[9].split(",") if item]

payload = {
    "schemaVersion": 1,
    "dryRun": False,
    "matrixPath": "config/phoneme_bakeoff_matrix.json",
    "offlineBakeoff": {
        "status": offline_status,
        "error": offline_error or None,
        "backends": offline_backends,
        "reportPath": str(offline_report) if offline_report.is_file() else None,
    },
    "liveDryRun": {
        "status": live_status,
        "error": live_error or None,
        "backends": live_backends,
        "comparisonPlanPath": str(live_plan) if live_plan.is_file() else None,
    },
}
if offline_report.is_file():
    payload["offlineBakeoff"]["report"] = json.loads(offline_report.read_text(encoding="utf-8"))

output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
print(f"Wrote {output}")
print(f"offline={offline_status} live_dry_run={live_status}")

failed = offline_status == "failed" or live_status == "failed"
raise SystemExit(1 if failed else 0)
PY
