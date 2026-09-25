#!/usr/bin/env python3
"""Validate the previously inspected asset snapshot and write private frame inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", type=Path, default=ROOT / "build/port-graphics/preview")
    parser.add_argument("--out", type=Path, default=ROOT / "build/port-rt64/frame-input.json")
    args = parser.parse_args()
    report = json.loads((args.assets / "export-report.json").read_text())
    path = args.assets / "ram.bin"
    ram = path.read_bytes()
    digest = hashlib.sha256(ram).hexdigest()
    if len(ram) != 0x800000 or digest != report["files"]["ram.bin"]["sha256"]:
        raise ValueError("RAM differs from the validated private asset export")
    if report["model"]["id"] != 35 or report["texture"]["id"] != "0x9097":
        raise ValueError("Wrong model/texture pair")
    model = int(report["model"]["cache_address"], 16) & 0x1FFFFFFF
    instance = int(report["model"]["instance_address"], 16) & 0x1FFFFFFF
    word = lambda address: struct.unpack_from(">I", ram, address)[0]
    vertex_base = word(instance + 4)
    if word(instance) & 0x1FFFFFFF != model or vertex_base != word(model + 0x1C):
        raise ValueError("Instance DMA base does not reference the decoded model vertices")
    list_address = word(model + 0x74)
    metadata = report["model"]["lists"]["0x74"]
    commands = ram[list_address & 0x1FFFFFFF:(list_address & 0x1FFFFFFF) + metadata["commands"] * 8]
    if hashlib.sha256(commands).hexdigest() != metadata["sha256"]:
        raise ValueError("Model display list differs from validated export")
    result = {"status": "prepared", "ram": str(path.resolve()), "ram_sha256": digest,
              "model": 35, "texture": "0x9097", "list_address": list_address,
              "vertex_base": vertex_base, "width": 320, "height": 240,
              "camera": "controlled_orthographic", "original_game_camera": False,
              "source_export_sha256": hashlib.sha256((args.assets / "export-report.json").read_bytes()).hexdigest()}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({k: result[k] for k in ("status", "ram_sha256", "list_address", "vertex_base")}))


if __name__ == "__main__":
    main()
