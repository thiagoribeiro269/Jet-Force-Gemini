#!/usr/bin/env python3
"""Check native animation frames, loop closure and the neutral-pose regression."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/port-native/animation"


def require(ok, message):
    if not ok:
        raise AssertionError(message)


def png_rgb(rgba):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    scan = b"".join(b"\0" + b"".join(rgba[(y * 640 + x) * 4:(y * 640 + x) * 4 + 3] for x in range(640)) for y in range(480))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 640, 480, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(scan)) + chunk(b"IEND", b""))


def main():
    result = json.loads((OUT / "rtx-result.json").read_text())
    meta = result["frame"]
    require(result["ok"] and result["math_test"]["exit_code"] == result["neutral"]["exit_code"] == 0, "Native runs failed")
    require((meta["api"], meta["vendor"], meta["frame_count"], meta["animation_id"], meta["source_keyframes"], meta["triangles"]) ==
            ("D3D11", 0x10DE, 33, 1026, 16, 534), "Wrong native animation profile")
    require(meta["animated"] and not meta["emulator_dependencies"] and not meta["display_list_interpreter"], "Wrong execution path")
    raw = (OUT / "frame.rgba").read_bytes()
    frame_size = 640 * 480 * 4
    require(len(raw) == 33 * frame_size, "Incomplete animation readback")
    neutral = (OUT / "neutral.rgba").read_bytes()
    # Independent previously approved output, not generated from this clip.
    require(hashlib.sha256(neutral).hexdigest() == "ac70cc84b273b79b814209eae1d0451d706ac19fdf914b7345013d8e4c4bacc6",
            "New skeleton's neutral pose changed the approved static frame")
    hashes, coverage, bounds = [], [], []
    for index in range(33):
        pixels = raw[index * frame_size:(index + 1) * frame_size]
        hashes.append(hashlib.sha256(pixels).hexdigest())
        lit = [p // 4 for p in range(0, len(pixels), 4) if pixels[p] | pixels[p + 1] | pixels[p + 2]]
        require(10000 < len(lit) < 80000, "Empty or implausible animated silhouette")
        box = [min(p % 640 for p in lit), min(p // 640 for p in lit), max(p % 640 for p in lit), max(p // 640 for p in lit)]
        require(0 < box[0] < box[2] < 639 and 0 < box[1] < box[3] < 479, "Animated character clipped by viewport")
        coverage.append(len(lit)); bounds.append(box)
        if index in (0, 8, 16, 24, 32):
            (OUT / f"frame-{index:03}.png").write_bytes(png_rgb(pixels))
    require(hashes[0] == hashes[32], "Animation loop does not return to the exact first GPU frame")
    require(len(set(hashes[:32])) == 32, "Animation frames did not advance uniquely")
    first, opposite = raw[:frame_size], raw[16 * frame_size:17 * frame_size]
    changed = sum(first[i:i + 3] != opposite[i:i + 3] for i in range(0, frame_size, 4))
    require(changed > 10000, "Frame changes do not establish substantial character motion")
    report = {"status": "passed", "clip_id": 1026, "keyframes": 16, "frames_rendered": 33,
              "unique_cycle_frames": 32, "loop_first_last_byte_equal": True,
              "neutral_pose_matches_previous_frame": True, "rgb_pixels_changed_half_cycle": changed,
              "coverage_range": [min(coverage), max(coverage)], "frame_bounds": bounds,
              "frame_sha256": hashes, "raw_stream_sha256": hashlib.sha256(raw).hexdigest(),
              "width": 640, "height": 480, "output_fps_controlled": 30, "source_keyframes_per_second_controlled": 15,
              "runtime": "Native C++ interpolation/hierarchy/mesh transforms and D3D11 project shaders",
              "limits": ["One source clip, controlled speed; no original timing claim",
                         "No blending, IK, animation events, gameplay, audio or physical controls",
                         "No MIPS/RT64/RSP/RDP/PIF/CIC emulation; no bit-exact original-animation execution comparison"]}
    # Three repetitions of the validated 32-frame cycle, omitting its closing duplicate.
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "rawvideo", "-pixel_format", "rgba", "-video_size", "640x480",
                    "-framerate", "30", "-i", str(OUT / "frame.rgba"),
                    "-vf", "trim=end_frame=32,loop=loop=2:size=32:start=0,setpts=N/30/TB",
                    "-an", "-c:v", "libx264", "-preset", "fast", "-crf", "18", "-pix_fmt", "yuv420p",
                    "-movflags", "+faststart", str(OUT / "juno-animation.mp4")], check=True)
    report["video_sha256"] = hashlib.sha256((OUT / "juno-animation.mp4").read_bytes()).hexdigest()
    (OUT / "animation-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: report[k] for k in ("status", "frames_rendered", "unique_cycle_frames", "loop_first_last_byte_equal",
                                           "neutral_pose_matches_previous_frame", "rgb_pixels_changed_half_cycle", "coverage_range")}))


if __name__ == "__main__":
    main()
