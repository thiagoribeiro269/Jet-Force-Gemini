#!/usr/bin/env python3
"""Validate recovered Juno selection decisions and their native GPU playback."""
import hashlib
import json
from pathlib import Path
import subprocess
from check_animation_frames import png_rgb, require

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/port-native/juno-selection"
SIZE = 640 * 480 * 4


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    result = json.loads((OUT / "rtx-result.json").read_text())
    meta = result["frame"]
    require(result["ok"] and result["juno_selection_test"]["exit_code"] == 0, "Native Juno selection run failed")
    tests = json.loads(result["juno_selection_test"]["stdout"])
    require(tests == {"status": "passed", "choice_cases": 18, "priority_cases": 8, "bridge_cases": 8,
                      "rejected_cases": 19, "original_table_cases": 20}, "Unexpected original-selection test coverage")
    require((meta["api"], meta["vendor"], meta["frame_count"], meta["clip_count"], meta["triangles"], meta["camera"]) ==
            ("D3D11", 0x10DE, 180, 9, 534, "juno_selection"), "Wrong Juno render profile")
    require(meta["juno_selection"] and not meta["emulator_dependencies"] and not meta["display_list_interpreter"], "Wrong runtime path")
    trace = result["controller_trace"]
    # frame, requested index, resolved index, global clip, transition profile, changed
    expected = [(0, 16, 16, 1019, 0, False), (24, 0, 0, 1026, 0, True), (42, 1, 1, 1027, 0, True),
                (60, 2, 2, 1028, 0, True), (78, 3, 3, 1025, 0, True), (96, 0, 36, 1061, 0, True),
                (114, 0, 28, 1055, 3, True), (132, 0, 36, 1061, 0, True), (150, 16, 16, 1019, 0, True),
                (168, 16, 16, 1019, 0, False)]
    fields = ("frame", "requested", "local", "id", "profile", "accepted")
    require([tuple(row[k] for k in fields) for row in trace["requests"]] == expected, "Recovered selection timeline differs")
    require(all(row["instant_matrix_delta"] == 0 for row in trace["requests"]), "A selection jumped the visible pose")
    states = trace["states"]
    require(len(states) == 180 and [s["frame"] for s in states] == list(range(180)), "Incomplete Juno trace")
    keys = {1019: 50, 1026: 16, 1027: 16, 1028: 16, 1025: 16, 1061: 16, 1055: 16}
    event = 0; initial_frame = 0; source_origin = 0
    for frame, state in enumerate(states):
        if event + 1 < len(expected) and frame == expected[event + 1][0]:
            event += 1
            if expected[event][-1]:
                initial_frame = frame; source_origin = 24.5 if frame == 150 else 0
        _, requested, local, clip, profile, _ = expected[event]
        require((state["requested"], state["local"], state["target"], state["profile"]) == (requested, local, clip, profile),
                "State did not retain the original selection")
        source = source_origin + (frame - initial_frame) * 0.5
        source = min(source, keys[clip] - 1) if clip == 1019 else source % keys[clip]
        require(abs(state["source_frame"] - source) < 1e-8, "Initial fraction, repetition or clip clock differs")
        if frame >= initial_frame + 6:
            require(not state["transitioning"] and state["weight"] == 1, "Native blend did not complete in 0.2 seconds")
    require(states[150]["source_frame"] == 24.5 and states[150]["weight"] == 0,
            "Original initial fraction was confused with native blend duration")
    require(states[168]["source_frame"] == 33.5 and states[168]["weight"] == 1, "Repeated selection restarted original phase")

    references = {
        "neutral.rgba": "ac70cc84b273b79b814209eae1d0451d706ac19fdf914b7345013d8e4c4bacc6",
        "cycle.rgba": json.loads((ROOT / "port/native/animation-validation.json").read_text())["animation"]["raw_stream_sha256"],
        "transitions.rgba": json.loads((ROOT / "port/native/transition-validation.json").read_text())["transition"]["raw_sha256"],
        "character.rgba": json.loads((ROOT / "port/native/character-validation.json").read_text())["character"]["raw_sha256"],
    }
    for name, reference in references.items():
        require(digest((OUT / name).read_bytes()) == reference, "Previous GPU proof changed: " + name)
    raw = (OUT / "frame.rgba").read_bytes()
    require(len(raw) == 180 * SIZE, "Incomplete Juno frame stream")
    coverage, bounds, hashes = [], [], []
    selected_samples = (0, 12, 24, 30, 48, 66, 84, 102, 120, 138, 150, 156, 168, 179)
    for frame in range(180):
        pixels = raw[frame * SIZE:(frame + 1) * SIZE]
        hashes.append(digest(pixels))
        lit = [p // 4 for p in range(0, len(pixels), 4) if pixels[p] | pixels[p + 1] | pixels[p + 2]]
        require(3000 < len(lit) < 80000, "Unexpected character coverage")
        box = [min(p % 640 for p in lit), min(p // 640 for p in lit), max(p % 640 for p in lit), max(p // 640 for p in lit)]
        require(0 < box[0] < box[2] < 639 and 0 < box[1] < box[3] < 479, "Juno clipped by the fixed camera")
        coverage.append(len(lit)); bounds.append(box)
        if frame in selected_samples:
            (OUT / f"frame-{frame:03}.png").write_bytes(png_rgb(pixels))
    require(len(set(hashes)) > 110, "Selected clips did not produce distinct animation")
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "rawvideo", "-pixel_format", "rgba", "-video_size", "640x480",
                    "-framerate", "30", "-i", str(OUT / "frame.rgba"), "-an", "-c:v", "libx264", "-preset", "fast",
                    "-crf", "18", "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(OUT / "juno-selection.mp4")], check=True)
    report = {"status": "passed", "frames": 180, "fps_controlled": 30, "duration_seconds": 6,
              "unique_frames": len(set(hashes)), "rendered_clips": sorted(keys), "requests": trace["requests"],
              "state_changes": 8, "unchanged_requests": 2, "instantaneous_selection_matrix_delta": 0,
              "initial_fraction_at_frame_150": 0.5, "source_frame_at_150": 24.5, "source_frame_at_repeat_168": 33.5,
              "native_blend_seconds": 0.2, "frame_bounds": bounds, "coverage_range": [min(coverage), max(coverage)],
              "all_four_previous_gpu_proofs_byte_equal": True, "raw_sha256": digest(raw),
              "video_sha256": digest((OUT / "juno-selection.mp4").read_bytes()),
              "limits": ["Two original selection decisions ported by static analysis; no MIPS execution or emulator reference",
                         "Diagnostic state inputs, supplied idle RNG result and controlled 15-source-frame/second clock",
                         "Transition profile is metadata; original controlSetTransition body is not implemented",
                         "Native blend duration is separate from original initial clip fraction",
                         "Nine decoded clips; unsupported destinations fail before changing selection",
                         "No whole boyControl loop, physics, scene, collision, weapons, audio or physical input"]}
    (OUT / "juno-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: report[k] for k in ("status", "frames", "unique_frames", "rendered_clips", "state_changes",
                                           "coverage_range", "all_four_previous_gpu_proofs_byte_equal")}))


if __name__ == "__main__":
    main()
