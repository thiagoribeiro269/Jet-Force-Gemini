#!/usr/bin/env python3
"""Check a real RTX character readback against the validated static input."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]
from export_frame import framebuffer_png


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def project_source(ram, frame):
    """CPU reference for positions only, from the original display-list bytes."""
    word = lambda at: struct.unpack_from(">I", ram, at & 0x1FFFFFFF)[0]
    matrix_slot, vertices, positions = {}, {}, []
    selected = None
    for i in range(353):
        at = frame["list_address"] + i * 8
        a, b = word(at), word(at + 4)
        opcode = a >> 24
        if opcode == 1:
            matrix_slot[(a >> 16) & 15] = struct.unpack_from(">16f", ram, (frame["matrix_base"] & 0x1FFFFFFF) + b)
        elif opcode == 0xBC:
            selected = matrix_slot[b // 64]
        elif opcode == 4:
            count, first = (a >> 19) & 31, (a >> 9) & 31
            for v in range(count):
                xyz = struct.unpack_from(">3h", ram, (frame["vertex_base"] & 0x1FFFFFFF) + b + v * 10)
                vertices[first + v] = tuple(x + selected[12 + k] for k, x in enumerate(xyz))
        elif opcode == 5:
            for t in range(((a >> 20) & 15) + 1):
                for index in ram[(b & 0x1FFFFFFF) + t * 16 + 1:(b & 0x1FFFFFFF) + t * 16 + 4]:
                    x, y, z = vertices[index]
                    cosine, sine = -0.939692621, -0.342020143
                    positions.append((160 + 160 * (cosine * x + sine * z) / 180,
                                      120 - 120 * (y - 113) / 135,
                                      (511 / 1024) * (1 + (-sine * x + cosine * z) / 1024)))
    return positions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "build/port-character")
    args = parser.parse_args()
    out = args.out.resolve()
    require(out.is_relative_to(ROOT / "build"), "Private frames must stay under ignored build/")
    result = json.loads((out / "rtx-result.json").read_text())
    source = json.loads((out / "source-validation.json").read_text())
    frame = json.loads((out / "frame-input.json").read_text())
    ram, original = (out / "ram.bin").read_bytes(), (out / "load-ram.bin").read_bytes()
    require(source["status"] == "passed" and source["pose"]["status"] == "passed", "CPU/pose proof missing")
    require(sha(ram) == frame["ram_sha256"] == source["frame_ram_sha256"] and
            sha(original) == source["load_ram_sha256"], "Input changed after source validation")
    start, length = source["pose"]["changed_range"]
    start &= 0x1FFFFFFF
    require(ram[:start] == original[:start] and ram[start + length:] == original[start + length:] and
            sha(ram[start:start + length]) == source["pose"]["matrix_sha256"], "Unexpected changes outside the prepared pose")
    require(result["ok"] and result["render"]["exit_code"] == 0 and not result["render"]["timed_out"], "GPU run failed")
    meta = result["frame"]
    require((meta["api"], meta["vendor"], meta["width"], meta["height"], meta["triangles"], meta["texture_loads"]) ==
            ("D3D12", 0x10DE, 320, 240, 502, 17), "Unexpected device/draw configuration")
    require(meta["pose"] == "neutral_bone_hierarchy_float_matrices", "Wrong pose path")
    require(meta["list_address"] == frame["list_address"] & 0x1FFFFFFF and
            meta["matrix_base"] == frame["matrix_base"] & 0x1FFFFFFF and
            meta["vertex_base"] == frame["vertex_base"] & 0x1FFFFFFF, "Render input addresses differ")
    expected = project_source(ram, frame)
    actual = result["cpu_diagnostic"]["after"]
    require(len(expected) == actual["face_indices_count"] == 1506, "Geometry submission count differs")
    samples = actual["pos_screen_sample"]
    require(len(samples) == 8, "Missing RT64 screen-position diagnostics")
    max_error = max(abs(a - b) for wanted, got in zip(expected, samples) for a, b in zip(wanted, got))
    require(max_error < 0.001, "RT64 sampled positions differ from bone/camera reference")
    pixels = (out / "frame.rgba").read_bytes()
    require(len(pixels) == 320 * 240 * 4, "Wrong readback size")
    colors = Counter(tuple(pixels[i:i + 4]) for i in range(0, len(pixels), 4))
    lit = [(i // 4 % 320, i // 4 // 320) for i in range(0, len(pixels), 4) if any(pixels[i:i + 3])]
    require(6000 < len(lit) < 14000 and len(colors) > 2000, "Empty, flat or implausible character frame")
    bounds = [min(x for x, y in lit), min(y for x, y in lit), max(x for x, y in lit), max(y for x, y in lit)]
    projected = [min(x for x, y, z in expected), min(y for x, y, z in expected),
                 max(x for x, y, z in expected), max(y for x, y, z in expected)]
    require(all(abs(a - b) < 3 for a, b in zip(bounds, projected)), "GPU coverage does not match the assembled character bounds")
    require(0 < bounds[0] < bounds[2] < 319 and 0 < bounds[1] < bounds[3] < 239, "Character clipped by frame edges")
    png = framebuffer_png(320, 240, pixels)
    (out / "frame.png").write_bytes(png)
    report = {"status": "passed", "frame_validated": True, "model": 220, "model_name": "Boy",
              "width": 320, "height": 240, "api": meta["api"], "device": meta["device"],
              "triangles": 502, "texture_loads": 17, "bones": 21,
              "colored_pixels": len(lit), "unique_rgba_colors": len(colors), "colored_bounds": bounds,
              "projected_geometry_bounds": projected, "sampled_RT64_positions": 8,
              "position_max_absolute_error": max_error, "rgba_sha256": sha(pixels), "png_sha256": sha(png),
              "input_ram_sha256": sha(ram), "pose_matrix_sha256": source["pose"]["matrix_sha256"],
              "png_format": "RGB8_opaque_display_view_original_RGB_preserved",
              "raw_alpha_histogram": dict(sorted(Counter(pixels[3::4]).items())),
              "limits": ["Static neutral skeleton and controlled orthographic camera/material/fog",
                         "Geometry bounds and eight transformed positions checked; no bit-exact RDP/hardware pixel reference",
                         "Real RT64 D3D12 framebuffer; no VI, gameplay, animation playback or original lighting",
                         "Neutral pose preparation checked against MIPS; animation CPU path is not yet integrated natively"]}
    (out / "frame-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
