#!/usr/bin/env python3
"""Export inspected native assets for local diagnosis, never for publication."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from checks_graphics import GraphicsSession
from graphics_asset_checks import check_graphics_assets, _dlist
from object_checks import check_objects
from checks_manager import require, load_native_library


def png_rgba(width, height, rgba):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    scanlines = b"".join(b"\0" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(scanlines)) + chunk(b"IEND", b""))


def run(args):
    # The repository intentionally excludes every artifact from this helper.
    root = HERE.parents[1]
    out = args.out.resolve()
    require(out.is_relative_to(root / "build"), "Private asset exports must remain under ignored build/")
    out.mkdir(parents=True, exist_ok=True)
    session = GraphicsSession(load_native_library(args.library), args.rom.read_bytes(),
                              json.loads(args.manifest.read_text()), dirty_heap=True)
    try:
        session.bootstrap()
        check_objects(session)
        result = session.run_function("modLoadModel", 35, 0)[2] & 0xFFFFFFFF
        report = check_graphics_assets(session, result)
        model = int(report["model"]["cache_address"], 16)
        texture = int(report["texture"]["cache_address"], 16)
        header = session.read(texture, 0x20)
        width, height = header[:2]
        raw = session.read(texture + 0x20, width * height * 2)
        command_pointer = session.word(texture + 0xC)
        command_count = int.from_bytes(header[0xA:0xC], "big")
        commands = list(struct.iter_unpack(">II", session.read(command_pointer, command_count * 8)))
        loads = [b for a, b in commands if a >> 24 == 0xF3]
        require(len(loads) == 1 and loads[0] & 0xFFF == 0 and (width * 2) % 8 == 0,
                "Preview only covers this RGBA16 LOADBLOCK with DXT zero")
        # LOADBLOCK with DXT=0 copies linearly into TMEM. Sampling a 16-bit
        # tile swaps adjacent 32-bit words on odd rows (wordIndex XOR 1).
        # Texture flag 4 alone is not used to infer the source arrangement.
        sampled = bytearray(raw)
        row_bytes = width * 2
        for y in range(1, height, 2):
            for x in range(row_bytes):
                sampled[y * row_bytes + x] = raw[y * row_bytes + (x ^ 4)]
        rgba = bytearray()
        for (pixel,) in struct.iter_unpack(">H", sampled):
            for shift in (11, 6, 1):
                value = (pixel >> shift) & 31
                rgba.append((value << 3) | (value >> 2))
            rgba.append(255 if pixel & 1 else 0)
        (out / "texture-9097.png").write_bytes(png_rgba(width, height, rgba))
        (out / "texture-9097.rgba16").write_bytes(raw)
        vertices = session.read(session.word(model + 0x1C), 4 * 10)
        triangles = session.read(session.word(model + 0x20), 2 * 16)
        lines = ["# Private diagnostic export of original model 35; not a rendered frame",
                 "mtllib model-35.mtl", "o model_35", "usemtl texture_9097"]
        for index in range(4):
            x, y, z = struct.unpack_from(">hhh", vertices, index * 10)
            lines.append(f"v {x} {y} {z}")
        for index in range(2):
            for corner in range(3):
                s, t = struct.unpack_from(">hh", triangles, index * 16 + 4 + corner * 4)
                lines.append(f"vt {s / (32 * width):.9g} {1 - t / (32 * height):.9g}")
        for index in range(2):
            indices = triangles[index * 16 + 1:index * 16 + 4]
            lines.append("f " + " ".join(f"{vertex + 1}/{index * 3 + corner + 1}" for corner, vertex in enumerate(indices)))
        (out / "model-35.obj").write_text("\n".join(lines) + "\n")
        (out / "model-35.mtl").write_text("newmtl texture_9097\nKd 1 1 1\nmap_Kd texture-9097.png\n")
        lists = {field: [[f"0x{a:08X}", f"0x{b:08X}"] for a, b in _dlist(session, session.word(model + int(field, 16)))]
                 for field in ("0x74", "0x78")}
        (out / "model-lists.json").write_text(json.dumps(lists, indent=2) + "\n")
        (out / "ram.bin").write_bytes(session.native.snapshot())
        report["export_limits"] = ["Decoded texture preview and OBJ only, no RT64/RSP/GPU rendering",
                                  "OBJ uses the model's raw positions and normalized triangle texture coordinates; no lighting or game material emulation"]
        report["preview_tmem_rule"] = "RGBA16 LOADBLOCK DXT=0: swap the two 32-bit halves of each 8-byte group on odd rows before RGBA5551 decoding"
        report["files"] = {p.name: {"bytes": p.stat().st_size, "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
                           for p in out.iterdir() if p.is_file() and p.name != "export-report.json"}
        (out / "export-report.json").write_text(json.dumps(report, indent=2) + "\n")
    finally:
        session.close()
    print(json.dumps({"private_directory": str(out), "files": list(report["files"]), "rendered_frame": False}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("library", "rom", "manifest", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    run(parser.parse_args())
