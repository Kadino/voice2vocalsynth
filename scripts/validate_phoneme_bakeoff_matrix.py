#!/usr/bin/env python3
"""Validate config/phoneme_bakeoff_matrix.json schema and invariants."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

REQUIRED_BACKEND_KEYS = {
    "id",
    "bakeoffName",
    "displayName",
    "license",
    "streamingKind",
    "arpabetOutput",
    "status",
    "offlineBakeoff",
    "liveVerification",
    "cannotPassLiveGates",
    "integrationPath",
    "comparePriority",
}

VALID_STATUS = {"integrated", "candidate"}
VALID_STREAMING = {
    "debug_only",
    "true_incremental",
    "windowed",
    "model_dependent",
}


def fail(message: str) -> None:
    print(f"error: {message}", file=sys.stderr)
    raise SystemExit(1)


def validate_backend(entry: dict[str, Any], index: int) -> None:
    missing = REQUIRED_BACKEND_KEYS - entry.keys()
    if missing:
        fail(f"backends[{index}] missing keys: {sorted(missing)}")

    if entry["status"] not in VALID_STATUS:
        fail(f"backends[{index}] has invalid status: {entry['status']}")

    if entry["streamingKind"] not in VALID_STREAMING:
        fail(f"backends[{index}] has invalid streamingKind: {entry['streamingKind']}")

    for flag in ("offlineBakeoff", "liveVerification", "cannotPassLiveGates"):
        if not isinstance(entry[flag], bool):
            fail(f"backends[{index}].{flag} must be a boolean")

    if not isinstance(entry["comparePriority"], int):
        fail(f"backends[{index}].comparePriority must be an integer")

    shell_name = entry.get("shellName")
    if entry["status"] == "integrated" and not shell_name:
        fail(f"integrated backends[{index}] must define shellName")

    if entry["id"] == "placeholder_pitch_gate" and not entry["cannotPassLiveGates"]:
        fail("placeholder_pitch_gate must set cannotPassLiveGates=true")


def validate_matrix(data: dict[str, Any]) -> dict[str, list[str]]:
    if data.get("schemaVersion") != 1:
        fail("schemaVersion must be 1")

    criteria = data.get("selectionCriteria")
    if not isinstance(criteria, list) or not criteria:
        fail("selectionCriteria must be a non-empty list")

    backends = data.get("backends")
    if not isinstance(backends, list) or not backends:
        fail("backends must be a non-empty list")

    ids: set[str] = set()
    for index, entry in enumerate(backends):
        if not isinstance(entry, dict):
            fail(f"backends[{index}] must be an object")
        validate_backend(entry, index)
        backend_id = entry["id"]
        if backend_id in ids:
            fail(f"duplicate backend id: {backend_id}")
        ids.add(backend_id)

    integrated_offline = [
        b["bakeoffName"]
        for b in backends
        if b["status"] == "integrated" and b["offlineBakeoff"]
    ]
    integrated_offline_runnable = [
        b["bakeoffName"]
        for b in backends
        if b["status"] == "integrated" and b["offlineBakeoff"] and not b.get("requiresOnnxModel")
    ]
    integrated_live = [
        b["shellName"]
        for b in backends
        if b["status"] == "integrated" and b["liveVerification"] and b.get("shellName")
    ]

    excluded = data.get("excluded", [])
    if excluded is not None and not isinstance(excluded, list):
        fail("excluded must be a list when present")

    return {
        "integratedOfflineBakeoffNames": integrated_offline,
        "integratedOfflineRunnableBakeoffNames": integrated_offline_runnable,
        "integratedLiveShellNames": integrated_live,
    }


def main() -> None:
    matrix_path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("config/phoneme_bakeoff_matrix.json")
    if not matrix_path.is_file():
        fail(f"matrix file not found: {matrix_path}")

    data = json.loads(matrix_path.read_text(encoding="utf-8"))
    summary = validate_matrix(data)
    print(f"Validated {matrix_path}")
    print(f"integrated offline bakeoff backends: {','.join(summary['integratedOfflineBakeoffNames'])}")
    print(
        "integrated offline runnable backends: "
        f"{','.join(summary['integratedOfflineRunnableBakeoffNames'])}"
    )
    print(f"integrated live shell backends: {','.join(summary['integratedLiveShellNames'])}")


if __name__ == "__main__":
    main()
