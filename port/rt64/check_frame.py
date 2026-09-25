#!/usr/bin/env python3
"""Validate the controlled GPU frame and save a private PNG and metadata."""
from collections import Counter
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/graphics"))
from export_frame import framebuffer_png


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "build/port-rt64")
    args = parser.parse_args()
    out = args.out.resolve()
    require(out.is_relative_to(ROOT / "build"), "Private frames must remain under ignored build/")
    result = json.loads((out / "rtx-result.json").read_text())
    meta = result["frame"]
    require(result["ok"] and result["render"]["exit_code"] == 0 and not result["render"]["timed_out"],
            "GPU process did not complete successfully")
    require(meta["api"] == "D3D12" and meta["vendor"] == 0x10DE and
            (meta["width"], meta["height"], meta["triangles"]) == (320, 240, 2), "Unexpected device or frame")
    pixels = (out / "frame.rgba").read_bytes()
    require(len(pixels) == 320 * 240 * 4, "Incorrect readback length")
    colors = Counter(tuple(pixels[i:i + 4]) for i in range(0, len(pixels), 4))
    lit = [(i // 4 % 320, i // 4 // 320) for i in range(0, len(pixels), 4) if any(pixels[i:i + 3])]
    require(22000 <= len(lit) <= 28000 and len(colors) >= 1000, "Empty, flat or unexpected quad coverage")
    bounds = [min(x for x, _ in lit), min(y for _, y in lit), max(x for x, _ in lit), max(y for _, y in lit)]
    require(all(abs(a - b) <= 2 for a, b in zip(bounds, (86, 26, 233, 214))), "Quad lies outside the controlled projection")

    # Independent sanity check against the decoded source texels. Bilinear
    # sampling approximates RT64's N64 three-point filter and output dithering;
    # this is not a bit-exact RDP/VI reference or a hardware accuracy claim.
    raw = (ROOT / "build/port-graphics/preview/texture-9097.rgba16").read_bytes()
    require(len(raw) == 4096, "Unexpected texture fixture")
    texture = []
    for y in range(64):
        row = []
        for x in range(32):
            address = y * 64 + ((x * 2) ^ (4 if y & 1 else 0))
            value = int.from_bytes(raw[address:address + 2], "big")
            row.append(tuple(((value >> shift) & 31) * 249.0 / 31.0 for shift in (11, 6, 1)))
        texture.append(row)
    state = result["cpu_diagnostic"]["after"]
    points, uv = state["pos_screen_sample"], state["tc_floats_sample"]
    require(len(points) == 6 and len(uv) == 12 and state["face_indices_count"] == 6,
            "CPU draw diagnostics do not describe two triangles")
    errors = []
    for y in range(32, 210, 7):
        for x in range(92, 230, 7):
            for j in (0, 3):
                a, b, c = points[j:j + 3]
                den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
                require(abs(den) > 1, "Degenerate controlled triangle")
                w0 = ((b[1] - c[1]) * (x + .5 - c[0]) + (c[0] - b[0]) * (y + .5 - c[1])) / den
                w1 = ((c[1] - a[1]) * (x + .5 - c[0]) + (a[0] - c[0]) * (y + .5 - c[1])) / den
                weights = (w0, w1, 1 - w0 - w1)
                if min(weights) < 0:
                    continue
                s = sum(w * uv[(j + k) * 2] for k, w in enumerate(weights))
                t = sum(w * uv[(j + k) * 2 + 1] for k, w in enumerate(weights))
                sx, ty = math.floor(s), math.floor(t)
                dx, dy = s - sx, t - ty
                samples = (((1 - dx) * (1 - dy), sx, ty), (dx * (1 - dy), sx + 1, ty),
                           ((1 - dx) * dy, sx, ty + 1), (dx * dy, sx + 1, ty + 1))
                for channel in range(3):
                    expected = sum(w * texture[min(63, max(0, yy))][min(31, max(0, xx))][channel]
                                   for w, xx, yy in samples)
                    errors.append(abs(pixels[(y * 320 + x) * 4 + channel] - expected))
                break
    require(len(errors) == 520 * 3, "Unexpected texture comparison sample count")
    mean_error = sum(errors) / len(errors)
    require(mean_error < 8.0, "GPU pixels do not resemble the source texture at the projected UVs")
    png = framebuffer_png(320, 240, pixels)
    (out / "frame.png").write_bytes(png)
    report = {"status": "passed", "frame_validated": True, "width": 320, "height": 240,
              "device": meta["device"], "api": "D3D12", "triangles": 2,
              "colored_pixels": len(lit), "unique_rgba_colors": len(colors), "colored_bounds": bounds,
              "texture_samples": len(errors) // 3, "texture_approximate_mean_absolute_error": mean_error,
              "texture_error_scale": "0..255 per RGB channel; bilinear approximation, not bit-exact RDP",
              "rgba_sha256": hashlib.sha256(pixels).hexdigest(), "png_sha256": hashlib.sha256(png).hexdigest(),
              "source_texture_sha256": hashlib.sha256(raw).hexdigest(),
              "png_format": "RGB8_opaque_display_view_original_RGB_preserved",
              "raw_alpha_histogram": dict(sorted(Counter(pixels[3::4]).items())),
              "limits": ["Controlled orthographic view, primitive/environment colors and zero fog factor",
                         "RT64 raw framebuffer only; no VI, display presentation, game boot, audio or gameplay",
                         "Bounded model-35 F3DJFG subset; specialized DXIL compilation disabled"]}
    (out / "frame-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
