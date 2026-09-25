#!/usr/bin/env python3
"""Validate application lifecycle, independent entities and the shared GPU backend."""
import hashlib
import json
from pathlib import Path
import subprocess
from check_animation_frames import png_rgb, require

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/port-native/integration"
SIZE = 640 * 480 * 4


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    result = json.loads((OUT / "rtx-result.json").read_text())
    require(result["ok"] and result["frame"]["integration"], "Native application integration failed")
    require(json.loads(result["session_test"]["stdout"]) == {"status": "passed", "session_cases": 9, "rejected_cases": 18,
            "presentation_rates": [30, 60, 144], "cadence_checkpoints": 12, "original_assets": True}, "Wrong session test coverage")
    session = result["session"]
    require((session["api"], session["vendor"], session["tick_rate"], session["output_fps"]) == ("D3D11", 0x10DE, 60, 30), "Wrong runtime/backend")
    require(session["stopped"] and not session["original_level"] and not session["emulation"], "Unexpected integration scope or teardown")
    frames = session["frames"]
    require(len(frames) == 180 and [f["host_tick"] for f in frames] == list(range(0, 360, 2)), "Presentation changed the simulation clock")
    for index, frame in enumerate(frames):
        require(frame["frame"] == index, "Incomplete integration frame trace")
        require(frame["generation"] == (1 if index <= 120 else 2), "Scene did not change at a tick boundary")
        require(frame["phase"] == ("paused" if 61 <= index <= 90 else "running"), "Wrong pause interval")
        expected_count = 2 if index <= 120 or 151 <= index <= 160 else 1
        require(len(frame["entities"]) == expected_count, "Entity lifecycle did not reach rendering")
        if 61 <= index <= 90:
            require(frame["world_tick"] == 120 and frame["entities"] == frames[60]["entities"], "Pause changed a pose, position or world clock")
        if index >= 161:
            require([e["slot"] for e in frame["entities"]] == [2], "Removed entity survived in a render snapshot")
    require(abs(frames[30]["entities"][0]["x"] + 96) < 1e-8, "First entity motion differs")
    require(frames[30]["entities"][1]["x"] == 120, "Input leaked to the second entity")
    require(abs(frames[60]["entities"][1]["z"] + 12) < 1e-8, "Second entity did not move independently")
    require(frames[121]["world_tick"] == 2 and frames[121]["entities"][0]["x"] == 0, "Scene replacement retained old state")
    require(abs(frames[-1]["entities"][0]["x"] - 116.4) < 1e-8, "Surviving entity did not consume its input")

    references = {
        "neutral.rgba": "ac70cc84b273b79b814209eae1d0451d706ac19fdf914b7345013d8e4c4bacc6",
        "cycle.rgba": json.loads((ROOT / "port/native/animation-validation.json").read_text())["animation"]["raw_stream_sha256"],
        "transitions.rgba": json.loads((ROOT / "port/native/transition-validation.json").read_text())["transition"]["raw_sha256"],
        "character.rgba": json.loads((ROOT / "port/native/character-validation.json").read_text())["character"]["raw_sha256"],
        "juno.rgba": json.loads((ROOT / "port/native/juno-selection-validation.json").read_text())["selection"]["raw_sha256"],
    }
    for name, reference in references.items():
        require(digest((OUT / name).read_bytes()) == reference, "Shared renderer changed a previous proof: " + name)
    raw = (OUT / "frame.rgba").read_bytes()
    require(len(raw) == 180 * SIZE, "Incomplete integration framebuffer stream")
    at = lambda frame: raw[frame * SIZE:(frame + 1) * SIZE]
    require(all(at(i) == at(60) for i in range(61, 91)), "Paused GPU frames are not identical")
    require(at(91) != at(90) and at(121) != at(120), "Resume or scene replacement was invisible")
    coverage, bounds, hashes = [], [], []
    samples = (0, 30, 45, 60, 76, 90, 91, 110, 120, 121, 135, 151, 160, 161, 179)
    for index in range(180):
        pixels = at(index); hashes.append(digest(pixels))
        lit = [p // 4 for p in range(0, len(pixels), 4) if pixels[p] | pixels[p + 1] | pixels[p + 2]]
        require(3000 < len(lit) < 100000, "Empty or unbounded native scene coverage")
        box = [min(p % 640 for p in lit), min(p // 640 for p in lit), max(p % 640 for p in lit), max(p // 640 for p in lit)]
        require(0 < box[0] < box[2] < 639 and 0 < box[1] < box[3] < 479, "Native scene clipped by viewport")
        coverage.append(len(lit)); bounds.append(box)
        if index in samples:
            (OUT / f"frame-{index:03}.png").write_bytes(png_rgb(pixels))
    require(len(set(hashes)) > 95, "Native world did not evolve enough")
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "rawvideo", "-pixel_format", "rgba", "-video_size", "640x480",
                    "-framerate", "30", "-i", str(OUT / "frame.rgba"), "-an", "-c:v", "libx264", "-preset", "fast",
                    "-crf", "18", "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(OUT / "native-session.mp4")], check=True)
    report = {"status": "passed", "frames": 180, "fps_output": 30, "simulation_tick_rate": 60, "duration_seconds": 6,
              "unique_frames": len(set(hashes)), "scenes_loaded": 2, "entities_created": 4, "maximum_simultaneous_entities": 2,
              "paused_frames_byte_equal": 31, "pause_preserved_state_despite_new_input": True,
              "removed_entity_not_rendered": True, "scene_replacement_atomic": True, "stopped_ownership_released": True,
              "all_five_previous_gpu_proofs_byte_equal": True, "coverage_range": [min(coverage), max(coverage)],
              "frame_bounds": bounds, "raw_sha256": digest(raw), "video_sha256": digest((OUT / "native-session.mp4").read_bytes()),
              "limits": ["Native application host and diagnostic scenes; not original level loading or whole gameplay",
                         "Original animation selection is integrated; motion, 60 Hz host policy and source clip rate remain controlled",
                         "No world collision, original physics, audio, saves, weapons, AI or physical input",
                         "One shared composite model resource, several independent entities; further mesh catalogs remain to integrate",
                         "No window, console hardware simulation or emulator code"]}
    (OUT / "integration-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: report[k] for k in ("status", "frames", "unique_frames", "scenes_loaded", "maximum_simultaneous_entities",
                                           "paused_frames_byte_equal", "all_five_previous_gpu_proofs_byte_equal")}))


if __name__ == "__main__":
    main()
