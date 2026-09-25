#!/usr/bin/env python3
"""Check native character commands, analytic motion endpoints and GPU regressions."""
import hashlib
import json
import math
from pathlib import Path
import subprocess
from check_animation_frames import png_rgb, require

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/port-native/character"
SIZE = 640 * 480 * 4


def digest(data):
    return hashlib.sha256(data).hexdigest()


def near(actual, expected, why):
    require(math.isfinite(actual) and abs(actual - expected) < 1e-7, why)


def main():
    result = json.loads((OUT / "rtx-result.json").read_text())
    meta = result["frame"]
    require(result["ok"] and result["character_test"]["exit_code"] == 0, "Native character/GPU run failed")
    tests = json.loads(result["character_test"]["stdout"])
    require(tests == {"status": "passed", "character_cases": 10, "rejected_cases": 20}, "Wrong character test coverage")
    require((meta["api"], meta["vendor"], meta["frame_count"], meta["clip_count"], meta["triangles"], meta["camera"]) ==
            ("D3D11", 0x10DE, 180, 3, 534, "character_path"), "Wrong character render profile")
    require(meta["character_commands"] and not meta["emulator_dependencies"] and not meta["display_list_interpreter"], "Wrong runtime path")
    trace = result["controller_trace"]
    expected_requests = [(0, False), (12, True), (24, False), (36, False), (48, True), (51, True), (72, True),
                         (84, True), (105, True), (117, True), (141, True), (150, True), (162, True)]
    require([(r["frame"], r["accepted"]) for r in trace["requests"]] == expected_requests, "State selection/repetition differs")
    require(all(r["instant_matrix_delta"] == 0 for r in trace["requests"]), "Command jumped the visible pose")
    states = trace["states"]
    require(len(states) == 180 and [s["frame"] for s in states] == list(range(180)), "Incomplete state trace")
    intervals = [(0, 12, "rest", 1071), (12, 48, "moving", 1026), (48, 51, "low", 1030), (51, 72, "moving", 1026),
                 (72, 84, "rest", 1071), (84, 105, "low", 1030), (105, 117, "rest", 1071),
                 (117, 141, "moving", 1026), (141, 150, "rest", 1071), (150, 162, "moving", 1026), (162, 180, "rest", 1071)]
    for first, end, state, clip in intervals:
        require(all(s["state"] == state and s["target"] == clip for s in states[first:end]), "State/clip policy differs")
        require(all(s["speed"] == (60 if state == "moving" else 0) for s in states[first:end]), "State did not control velocity")
    require(states[24]["source_frame"] == 6 and states[36]["source_frame"] == 12, "Held or directional input reset animation")
    require(states[50]["transitioning"] and states[51]["target"] == 1026 and states[51]["weight"] == 0,
            "Low-posture blend was not interrupted by movement")
    # Independent analytic endpoints: full-speed straight and 45-degree segments.
    diagonal = 66 / math.sqrt(2)
    endpoints = {12: (0, 0), 36: (48, 0), 48: (48 + 24 / math.sqrt(2), -24 / math.sqrt(2)),
                 51: (48 + 24 / math.sqrt(2), -24 / math.sqrt(2)), 72: (48 + diagonal, -diagonal),
                 117: (48 + diagonal, -diagonal), 141: (diagonal, -diagonal),
                 150: (diagonal, -diagonal), 162: (diagonal, -diagonal - 24), 179: (diagonal, -diagonal - 24)}
    for frame, (x, z) in endpoints.items():
        near(states[frame]["x"], x, "World X endpoint differs")
        near(states[frame]["z"], z, "World Z endpoint differs")
    total_distance, max_turn = 0, 0
    for previous, current in zip(states, states[1:]):
        distance = math.hypot(current["x"] - previous["x"], current["z"] - previous["z"])
        near(distance, 2 if previous["state"] == "moving" else 0, "Wrong per-step distance or motion while stopped")
        turn = abs(math.remainder(current["yaw"] - previous["yaw"], 2 * math.pi))
        require(turn <= math.pi / 30 + 1e-9, "Turn exceeded the configured rate")
        if previous["state"] != "moving":
            near(turn, 0, "Stopped character changed heading")
        total_distance += distance; max_turn = max(max_turn, turn)
    near(total_distance, 186, "Incorrect total path length")
    # The last 0.4 s movement turns 72 degrees from +90; stopping holds +18.
    near(states[-1]["yaw"], math.pi / 10, "Short final movement or stop violated the turn-rate policy")
    require(not states[-1]["transitioning"], "Final rest did not settle")

    neutral = (OUT / "neutral.rgba").read_bytes()
    require(digest(neutral) == "ac70cc84b273b79b814209eae1d0451d706ac19fdf914b7345013d8e4c4bacc6", "Neutral pose regression")
    cycle = (OUT / "cycle.rgba").read_bytes()
    prior_cycle = json.loads((ROOT / "port/native/animation-validation.json").read_text())["animation"]["raw_stream_sha256"]
    require(digest(cycle) == prior_cycle, "Original animation cycle regression")
    transitions = (OUT / "transitions.rgba").read_bytes()
    prior_transitions = json.loads((ROOT / "port/native/transition-validation.json").read_text())["transition"]["raw_sha256"]
    require(digest(transitions) == prior_transitions, "Two-clip transition regression")
    raw = (OUT / "frame.rgba").read_bytes()
    require(len(raw) == 180 * SIZE, "Incomplete character framebuffer stream")
    frame_at = lambda index: raw[index * SIZE:(index + 1) * SIZE]
    require(all(frame_at(index) == frame_at(0) for index in range(12)), "Constant rest pose drifted")
    require(all(frame_at(index) == frame_at(179) for index in range(170, 180)), "Settled character did not stop")
    hashes, coverage, bounds = [], [], []
    for index in range(180):
        pixels = frame_at(index)
        hashes.append(digest(pixels))
        lit = [p // 4 for p in range(0, len(pixels), 4) if pixels[p] | pixels[p + 1] | pixels[p + 2]]
        require(3000 < len(lit) < 80000, "Unexpected character coverage")
        box = [min(p % 640 for p in lit), min(p // 640 for p in lit), max(p % 640 for p in lit), max(p // 640 for p in lit)]
        require(0 < box[0] < box[2] < 639 and 0 < box[1] < box[3] < 479, "Character clipped by camera")
        coverage.append(len(lit)); bounds.append(box)
        if index in (0, 12, 20, 36, 48, 51, 60, 72, 80, 84, 95, 113, 129, 149, 162, 179):
            (OUT / f"frame-{index:03}.png").write_bytes(png_rgb(pixels))
    require(len(set(hashes)) > 110, "Commands did not produce enough distinct frames")
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "rawvideo", "-pixel_format", "rgba", "-video_size", "640x480",
                    "-framerate", "30", "-i", str(OUT / "frame.rgba"), "-an", "-c:v", "libx264", "-preset", "fast",
                    "-crf", "18", "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(OUT / "juno-character.mp4")], check=True)
    report = {"status": "passed", "frames": 180, "unique_frames": len(set(hashes)), "fps_controlled": 30, "duration_seconds": 6,
              "policy": {"rest": 1071, "moving": 1026, "low": 1030, "speed": 60, "dead_zone": 0.15,
                         "turn_radians_per_second": math.pi, "blend_seconds": 0.25, "ground_plane_y": 0},
              "input_requests": 13, "state_changes": 10, "commands_preserving_selected_clip": 3,
              "interrupted_transition": True, "instantaneous_selection_matrix_delta": 0,
              "diagonal_speed_normalized": True, "low_posture_overrides_movement": True, "dead_zone_stops_movement": True,
              "path_length": total_distance, "final_position_xz": [states[-1]["x"], states[-1]["z"]],
              "maximum_turn_per_frame": max_turn, "analytic_endpoints_checked": len(endpoints),
              "neutral_byte_equal_to_prior": True, "prior_cycle_33_frames_byte_equal": True, "prior_transitions_96_frames_byte_equal": True,
              "constant_rest_frames_identical": 12, "settled_rest_frames_identical": 10,
              "coverage_range": [min(coverage), max(coverage)], "frame_bounds": bounds, "raw_sha256": digest(raw),
              "video_sha256": digest((OUT / "juno-character.mp4").read_bytes()),
              "limits": ["Scripted native world-space inputs, no physical controller or interactive window",
                         "State names and movement/turn/blend parameters are prototype policy, not reconstructed original gameplay",
                         "Original clip 1071 is used as a provisional constant stance; its original semantic name is unknown",
                         "No collision, gravity, root-motion locomotion, foot planting, audio or animation events",
                         "No emulator or original MIPS execution; matching ROM validation is separate"]}
    (OUT / "character-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: report[k] for k in ("status", "frames", "unique_frames", "state_changes", "path_length",
                                           "final_position_xz", "coverage_range", "prior_transitions_96_frames_byte_equal")}))


if __name__ == "__main__":
    main()
