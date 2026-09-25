#!/usr/bin/env python3
"""Validate the native GPU readback and export an opaque private PNG."""
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parents[2]
out = ROOT / "build/port-native"
result = json.loads((out / "rtx-result.json").read_text())
meta = result["frame"]
if not result["ok"] or (meta["api"], meta["vendor"], meta["width"], meta["height"], meta["triangles"]) != ("D3D11", 0x10DE, 640, 480, 534):
    raise ValueError("Native hardware run did not pass")
if meta["emulator_dependencies"] or meta["display_list_interpreter"]:
    raise ValueError("Wrong graphics architecture")
raw = (out / "frame.rgba").read_bytes()
if len(raw) != 640 * 480 * 4:
    raise ValueError("Wrong readback length")
lit = [(i // 4 % 640, i // 4 // 640) for i in range(0, len(raw), 4) if any(raw[i:i + 3])]
bounds = [min(x for x, y in lit), min(y for x, y in lit), max(x for x, y in lit), max(y for x, y in lit)]
colors = Counter(tuple(raw[i:i + 3]) for i in range(0, len(raw), 4))
if not (20000 < len(lit) < 60000 and len(colors) > 1000 and
        all(abs(a - b) <= 5 for a, b in zip(bounds, (166, 39, 481, 441)))):
    raise ValueError(f"Unexpected native character coverage: {len(lit)}, {bounds}")

def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

rows = []
for y in range(480):
    rows.append(b"\0" + b"".join(raw[(y * 640 + x) * 4:(y * 640 + x) * 4 + 3] for x in range(640)))
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 640, 480, 8, 2, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(b"".join(rows))) + chunk(b"IEND", b"")
(out / "frame.png").write_bytes(png)
report = {"status": "passed", "api": "D3D11", "width": 640, "height": 480,
          "triangles": 534, "draws": meta["draws"], "textures": meta["textures"],
          "colored_pixels": len(lit), "unique_rgb_colors": len(colors), "colored_bounds": bounds,
          "rgba_sha256": hashlib.sha256(raw).hexdigest(), "png_sha256": hashlib.sha256(png).hexdigest(),
          "png": "RGB8 opaque, exact GPU RGB values", "runtime": "Native C++ and Windows D3D11 with project HLSL shaders",
          "limits": ["Static neutral pose and controlled camera", "Ordinary PC materials; no N64 pixel equivalence",
                     "No game loop, audio, controls or animation playback", "No MIPS/RT64/RSP/RDP/CIC/PIF execution"]}
(out / "frame-validation.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report))
