#!/usr/bin/env python3
"""Regenerate tests/fixtures/onnx/dummy_identity.onnx. See tests/fixtures/onnx/PROVENANCE.md."""
from __future__ import annotations

import os

import onnx
from onnx import TensorProto, helper


def main() -> None:
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_path = os.path.join(repo_root, "tests", "fixtures", "onnx", "dummy_identity.onnx")

    inp = helper.make_tensor_value_info("input", TensorProto.FLOAT, [1, 4, 8])
    out = helper.make_tensor_value_info("output", TensorProto.FLOAT, [1, 4, 8])
    node = helper.make_node("Identity", inputs=["input"], outputs=["output"])
    graph = helper.make_graph([node], "dummy_identity", [inp], [out])
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 13)])
    onnx.checker.check_model(model, full_check=True)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    onnx.save(model, out_path)
    print("Wrote", out_path, "size", os.path.getsize(out_path))


if __name__ == "__main__":
    main()
