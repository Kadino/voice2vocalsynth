#!/usr/bin/env bash
# Run multiple backends through the formal live path and rank their reports.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERIFY_ROOT="${LIVE_PHONEME_VERIFY_ROOT:-${HOME}/.local/share/Voice2VocalSynth/LivePhonemeVerify}"
BACKENDS="placeholder,pocketsphinx"
SUBSET=20
OUTPUT=""
ONNX_MODEL=""
ONNX_CONFIG=""

usage() {
  cat <<EOF
Usage: $(basename "$0") [--backends comma,separated,list] [--subset N]
       [--output comparison.json] [--onnx-model FILE --onnx-config FILE]

Every backend is replayed through the JUCE live path. Placeholder is retained
as a negative baseline and cannot pass. The command exits non-zero when no
candidate backend passes all gates.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --help|-h) usage; exit 0 ;;
    --backends) BACKENDS="${2:?missing value for --backends}"; shift 2 ;;
    --subset) SUBSET="${2:?missing value for --subset}"; shift 2 ;;
    --output) OUTPUT="${2:?missing value for --output}"; shift 2 ;;
    --onnx-model) ONNX_MODEL="${2:?missing value for --onnx-model}"; shift 2 ;;
    --onnx-config) ONNX_CONFIG="${2:?missing value for --onnx-config}"; shift 2 ;;
    *) echo "error: unknown argument: $1" >&2; usage >&2; exit 1 ;;
  esac
done

comparison_dir="${VERIFY_ROOT}/runs/$(date -u +%Y%m%dT%H%M%SZ)-comparison"
mkdir -p "${comparison_dir}"
if [[ -z "${OUTPUT}" ]]; then
  OUTPUT="${comparison_dir}/comparison.json"
fi

IFS=',' read -r -a backend_list <<<"${BACKENDS}"
metrics_files=()
for backend in "${backend_list[@]}"; do
  backend="${backend//[[:space:]]/}"
  [[ -z "${backend}" ]] && continue
  run_dir="${comparison_dir}/${backend}"
  args=(--backend "${backend}" --subset "${SUBSET}" --run-dir "${run_dir}")
  if [[ "${backend}" == "onnx_phoneme" ]]; then
    if [[ -z "${ONNX_MODEL}" ]]; then
      echo "error: comparison includes onnx_phoneme but --onnx-model was not provided" >&2
      exit 1
    fi
    args+=(--onnx-model "${ONNX_MODEL}")
    if [[ -n "${ONNX_CONFIG}" ]]; then
      args+=(--onnx-config "${ONNX_CONFIG}")
    fi
  fi
  set +e
  "${SCRIPT_DIR}/run_live_phoneme_verify_linux.sh" "${args[@]}"
  status=$?
  set -e
  if [[ "${status}" -ne 0 && "${status}" -ne 3 ]]; then
    echo "error: live run failed before scoring for backend ${backend}" >&2
    exit "${status}"
  fi
  metrics_files+=("${run_dir}/metrics.json")
done

python3 - "${OUTPUT}" "${metrics_files[@]}" <<'PY'
import json, pathlib, sys

output = pathlib.Path(sys.argv[1])
rows = []
for path_text in sys.argv[2:]:
    path = pathlib.Path(path_text)
    data = json.loads(path.read_text(encoding="utf-8"))
    rows.append({
        "backend": data["backend"],
        "passed": data["gates"]["passed"],
        "f1": data["quality"]["f1"],
        "p95OnsetErrorMs": data["quality"]["p95OnsetErrorMs"],
        "missedConsonantRate": data["quality"]["missedConsonantRate"],
        "endToEndMs": data["latency"]["endToEndMs"],
        "metricsPath": str(path),
    })
rows.sort(key=lambda row: (not row["passed"], -row["f1"], row["p95OnsetErrorMs"]))
payload = {"schemaVersion": 1, "liveVerification": True, "backends": rows}
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
print(f"{'backend':28} {'pass':5} {'f1':>8} {'p95 onset':>12} {'e2e ms':>10}")
for row in rows:
    print(f"{row['backend']:28} {str(row['passed']):5} {row['f1']:8.3f} "
          f"{row['p95OnsetErrorMs']:12.1f} {row['endToEndMs']:10.1f}")
print(f"Wrote {output}")
raise SystemExit(0 if any(row["passed"] for row in rows) else 3)
PY
