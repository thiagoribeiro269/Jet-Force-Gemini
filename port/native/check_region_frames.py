#!/usr/bin/env python3
"""Check original region + character composition and all native GPU regressions."""
import hashlib
import json
from pathlib import Path
import subprocess
from check_animation_frames import png_rgb, require

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/port-native/region"
SIZE = 640 * 480 * 4


def digest(data):
    return hashlib.sha256(data).hexdigest()


def changed(a, b):
    return [offset // 4 for offset in range(0, SIZE, 4) if a[offset:offset + 3] != b[offset:offset + 3]]


def main():
    result = json.loads((OUT / "rtx-result.json").read_text()); meta = result["frame"]; trace = result["region_trace"]
    require(result["ok"] and json.loads(result["region_test"]["stdout"]) ==
            {"status": "passed", "geometry_camera_cases": 6, "rejected_cases": 11, "original_region": True}, "Region checks failed")
    require((meta["api"], meta["vendor"], meta["region"], meta["geometry"], meta["terrain_triangles"], meta["character_triangles"], meta["resource_count"]) ==
            ("D3D11", 0x10DE, 21, 17, 2018, 534, 2), "Wrong original region render profile")
    require(meta["perspective"] and not meta["emulator_dependencies"] and not meta["display_list_interpreter"], "Wrong graphics path")
    require(trace["source_spawn"] == [40, 19, 841] and abs(trace["model_scale"] - 0.26) < 1e-7 and trace["ground_y"] == -2,
            "Original placement/ground evidence differs")
    require(trace["stopped"] and trace["original_region"] and not trace["original_gameplay"] and not trace["emulation"], "Wrong scope or cleanup")
    require(len(trace["frames"]) == 180, "Incomplete region timeline")
    for index, frame in enumerate(trace["frames"]):
        require((frame["frame"], frame["tick"], frame["world_meshes"], frame["entities"], frame["clip"], frame["source_frame"]) ==
                (index, index * 2, 1, 1, 1019, min(index * 0.5, 49)), "Region/actor left the shared host")
    references = {
        "neutral.rgba": "ac70cc84b273b79b814209eae1d0451d706ac19fdf914b7345013d8e4c4bacc6",
        "cycle.rgba": json.loads((ROOT / "port/native/animation-validation.json").read_text())["animation"]["raw_stream_sha256"],
        "transitions.rgba": json.loads((ROOT / "port/native/transition-validation.json").read_text())["transition"]["raw_sha256"],
        "character.rgba": json.loads((ROOT / "port/native/character-validation.json").read_text())["character"]["raw_sha256"],
        "juno.rgba": json.loads((ROOT / "port/native/juno-selection-validation.json").read_text())["selection"]["raw_sha256"],
        "integration.rgba": json.loads((ROOT / "port/native/integration-validation.json").read_text())["integration"]["raw_sha256"],
    }
    for name, expected in references.items():
        require(digest((OUT / name).read_bytes()) == expected, "Native catalog/camera regression: " + name)
    background = (OUT / "frame.background.rgba").read_bytes(); terrain = (OUT / "frame.terrain.rgba").read_bytes(); actor = (OUT / "frame.character.rgba").read_bytes()
    require(all(len(p) == SIZE for p in (background, terrain, actor)), "Missing independent resource captures")
    require(background == background[:4] * (640 * 480), "Empty renderer frame is not the controlled backdrop")
    raw = (OUT / "frame.rgba").read_bytes(); require(len(raw) == 180 * SIZE, "Incomplete original region video stream")
    first = raw[:SIZE]
    terrain_pixels, actor_pixels = changed(terrain, background), changed(actor, background)
    contribution = changed(first, terrain)
    require(len(terrain_pixels) > 30000 and 200 < len(actor_pixels) < 30000 and len(contribution) > 150,
            "Region or character failed to contribute visibly")
    box = [min(p % 640 for p in actor_pixels), min(p // 640 for p in actor_pixels), max(p % 640 for p in actor_pixels), max(p // 640 for p in actor_pixels)]
    require(0 < box[0] < box[2] < 639 and 0 < box[1] < box[3] < 479, "Character clipped by perspective camera")
    outside = [p for p in contribution if not (box[0] <= p % 640 <= box[2] and box[1] <= p // 640 <= box[3])]
    require(len(outside) <= 4, "Adding the character changed unrelated parts of the world")
    coverage, hashes = [], []
    for frame in range(180):
        pixels = raw[frame * SIZE:(frame + 1) * SIZE]; hashes.append(digest(pixels))
        covered = sum(pixels[p:p + 3] != background[p:p + 3] for p in range(0, SIZE, 4))
        require(covered > 30000, "Camera lost the region")
        coverage.append(covered)
        if frame in (0, 30, 45, 60, 90, 120, 150, 179): (OUT / f"frame-{frame:03}.png").write_bytes(png_rgb(pixels))
    require(len(set(hashes)) > 150, "Perspective sequence did not evolve")
    for name, pixels in (("terrain", terrain), ("character", actor), ("background", background)):
        (OUT / f"frame-{name}.png").write_bytes(png_rgb(pixels))
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "rawvideo", "-pixel_format", "rgba", "-video_size", "640x480",
                    "-framerate", "30", "-i", str(OUT / "frame.rgba"), "-an", "-c:v", "libx264", "-preset", "fast",
                    "-crf", "18", "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(OUT / "forest-first.mp4")], check=True)
    report = {"status": "passed", "level": 21, "geometry": 17, "frames": 180, "unique_frames": len(set(hashes)),
              "terrain_pixels_first_frame": len(terrain_pixels), "character_pixels_in_isolation": len(actor_pixels),
              "character_contribution_in_world": len(contribution), "changed_pixels_outside_character_box": len(outside),
              "character_box_first_frame": box, "region_coverage_range": [min(coverage), max(coverage)],
              "source_spawn": trace["source_spawn"], "original_model_scale": trace["model_scale"], "ground_height": trace["ground_y"],
              "native_visual_offset_y": trace["visual_offset_y"], "source_y_preserved_separately_from_visual_alignment": True,
              "shared_session_region_and_character": True, "all_six_previous_gpu_proofs_byte_equal": True,
              "raw_sha256": digest(raw), "terrain_capture_sha256": digest(terrain), "character_capture_sha256": digest(actor),
              "video_sha256": digest((OUT / "forest-first.mp4").read_bytes()), "fps_output": 30, "duration_seconds": 6,
              "limits": ["Original region geometry/material inputs in the native host, not full original levelInit or playable gameplay",
                         "Controlled perspective camera and native repeated-UV/alpha material policy; original sky/effects and object behaviors omitted",
                         "Original source position and scale retained; visual foot alignment uses a native vertical ground query, not original gravity",
                         "No wall collision, swept character collision, audio, enemies or physical input",
                         "No emulation or emulator code; ROM and derived assets remain private"]}
    (OUT / "region-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: report[k] for k in ("status", "frames", "unique_frames", "terrain_pixels_first_frame",
                                           "character_contribution_in_world", "all_six_previous_gpu_proofs_byte_equal")}))


if __name__ == "__main__":
    main()
