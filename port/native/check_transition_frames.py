#!/usr/bin/env python3
"""Validate the native controller trace, GPU regressions and transition preview."""
import hashlib
import json
from pathlib import Path
import subprocess
from check_animation_frames import png_rgb, require

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/port-native/transitions"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    result = json.loads((OUT / "rtx-result.json").read_text())
    meta = result["frame"]
    require(result["ok"] and result["controller_test"]["exit_code"] == 0, "Native controller/GPU run failed")
    require((meta["api"], meta["vendor"], meta["frame_count"], meta["clip_count"], meta["triangles"]) ==
            ("D3D11", 0x10DE, 96, 2, 534), "Wrong transition render profile")
    require(meta["sequence"] and not meta["emulator_dependencies"] and not meta["display_list_interpreter"], "Wrong runtime path")
    require(meta["camera"] == "transition_wide" and result["wide_cycle"]["exit_code"] == 0, "Wrong transition camera/reference")
    trace = result["controller_trace"]
    expected = [(32, 1030, True), (34, 1030, False), (36, 1026, True), (56, 1030, True), (80, 1026, True)]
    require([(r["frame"], r["id"], r["accepted"]) for r in trace["requests"]] == expected, "Selection acceptance differs")
    require(all(r["instant_matrix_delta"] == 0 for r in trace["requests"]), "A selection changed the instantaneous pose")
    states = trace["states"]
    require(len(states) == 96 and [s["frame"] for s in states] == list(range(96)), "Incomplete controller timeline")
    require(states[34]["source_frame"] == 1 and states[34]["weight"] > states[33]["weight"], "Repeated command restarted time/blend")
    require(states[35]["transitioning"] and states[36]["target"] == 1026 and states[36]["weight"] == 0,
            "The interruption did not replace a running transition")
    for frame, clip, phase in ((42, 1026, 3), (65, 1030, 4.5), (88, 1026, 4)):
        require(not states[frame]["transitioning"] and states[frame]["target"] == clip and
                states[frame]["source_frame"] == phase, "Transition endpoint or clip clock differs")
    neutral = (OUT / "neutral.rgba").read_bytes()
    require(digest(neutral) == "ac70cc84b273b79b814209eae1d0451d706ac19fdf914b7345013d8e4c4bacc6", "Neutral pose regression")
    cycle = (OUT / "cycle.rgba").read_bytes()
    prior = json.loads((ROOT / "port/native/animation-validation.json").read_text())["animation"]["raw_stream_sha256"]
    require(digest(cycle) == prior, "Previous 33-frame animation changed")
    raw = (OUT / "frame.rgba").read_bytes()
    size = 640 * 480 * 4
    require(len(raw) == 96 * size and len(cycle) == 33 * size, "Incomplete native framebuffer stream")
    wide_cycle = (OUT / "cycle-wide.rgba").read_bytes()
    require(len(wide_cycle) == 33 * size, "Incomplete camera-specific cycle reference")
    frame_at = lambda number: raw[number * size:(number + 1) * size]
    cycle_at = lambda number: wide_cycle[number * size:(number + 1) * size]
    require(raw[:32 * size] == wide_cycle[:32 * size], "Controller playback differs before the first selection")
    require(frame_at(32) == cycle_at(0), "First blend did not preserve its starting GPU pose")
    require(frame_at(42) == cycle_at(6) and frame_at(88) == cycle_at(8), "Completed blend differs from destination-only rendering")
    coverage, bounds, hashes = [], [], []
    for index in range(96):
        pixels = frame_at(index)
        hashes.append(digest(pixels))
        lit = [p // 4 for p in range(0, len(pixels), 4) if pixels[p] | pixels[p + 1] | pixels[p + 2]]
        require(10000 < len(lit) < 80000, "Unexpected character coverage")
        box = [min(p % 640 for p in lit), min(p // 640 for p in lit), max(p % 640 for p in lit), max(p // 640 for p in lit)]
        require(0 < box[0] < box[2] < 639 and 0 < box[1] < box[3] < 479, "Character clipped by viewport")
        coverage.append(len(lit)); bounds.append(box)
        if index in (0, 32, 35, 36, 39, 42, 56, 60, 65, 70, 80, 84, 88, 95):
            (OUT / f"frame-{index:03}.png").write_bytes(png_rgb(pixels))
    require(len(set(hashes)) > 45, "Sequence did not exercise enough distinct poses")
    second_clip, reference = frame_at(65), cycle_at(0)
    changed = sum(second_clip[i:i + 3] != reference[i:i + 3] for i in range(0, size, 4))
    require(changed > 10000, "Second clip did not produce visibly distinct motion")
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "rawvideo", "-pixel_format", "rgba", "-video_size", "640x480",
                    "-framerate", "30", "-i", str(OUT / "frame.rgba"), "-an", "-c:v", "libx264", "-preset", "fast",
                    "-crf", "18", "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(OUT / "juno-transitions.mp4")], check=True)
    report = {"status": "passed", "clip_ids": [1026, 1030], "frames": 96, "unique_frames": len(set(hashes)),
              "requests": trace["requests"], "accepted_switches": 4, "repeated_requests_ignored": 1,
              "interrupted_transition": True, "instantaneous_selection_matrix_delta": 0,
              "neutral_byte_equal_to_prior": True, "prior_cycle_33_frames_byte_equal": True,
              "first_32_controller_frames_match_cycle": True, "completed_blend_frames_match_destination": [42, 88],
              "coverage_range": [min(coverage), max(coverage)], "rgb_pixels_changed_second_clip": changed,
              "camera": "transition_wide", "original_camera_retained_for_prior_regressions": True,
              "frame_bounds": bounds, "raw_sha256": digest(raw), "video_sha256": digest((OUT / "juno-transitions.mp4").read_bytes()),
              "fps_controlled": 30, "duration_seconds": 3.2,
              "limits": ["Scripted PC clip-selection commands, no physical controller or gameplay state",
                         "Native local-angle blend from a captured source pose toward an advancing destination; original blend algorithm not claimed",
                         "Pose continuity is tested at selection; velocity continuity and foot planting/IK are not guaranteed",
                         "No emulation, audio, animation events, locomotion or collision"]}
    (OUT / "transition-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: report[k] for k in ("status", "frames", "unique_frames", "accepted_switches", "instantaneous_selection_matrix_delta",
                                           "neutral_byte_equal_to_prior", "prior_cycle_33_frames_byte_equal", "coverage_range")}))


if __name__ == "__main__":
    main()
