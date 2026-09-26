#!/usr/bin/env python3
"""Check the Forest First movement proof, its trace and every earlier GPU regression."""
import hashlib
import json
from pathlib import Path
import subprocess
from check_animation_frames import png_rgb, require

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/port-native/movement"
SIZE = 640 * 480 * 4
FRAMES = 580


def digest(data):
    return hashlib.sha256(data).hexdigest()


def changed(a, b):
    return [offset // 4 for offset in range(0, SIZE, 4) if a[offset:offset + 3] != b[offset:offset + 3]]


def main():
    result = json.loads((OUT / "rtx-result.json").read_text()); meta = result["frame"]; trace = result["movement_trace"]
    linux = json.loads((OUT / "linux-checks.json").read_text())["check_movement"]
    windows = json.loads(result["movement_test"]["stdout"])
    require(result["ok"] and windows == linux and windows["status"] == "passed" and windows["original_region"],
            "Movement checks failed or differ between Linux and Windows")
    require((meta["api"], meta["vendor"], meta["region"], meta["geometry"], meta["frame_count"], meta["ticks"]) ==
            ("D3D11", 0x10DE, 21, 17, FRAMES, 1160), "Wrong movement render profile")
    require(meta["perspective"] and not meta["emulator_dependencies"] and not meta["display_list_interpreter"], "Wrong graphics path")
    require(trace["stopped"] and trace["original_movement"] and trace["original_camera"] and not trace["object_collision"] and not trace["emulation"],
            "Wrong scope or cleanup")
    frames = trace["frames"]; require(len(frames) == FRAMES, "Incomplete movement timeline")
    for index, frame in enumerate(frames):
        require((frame["frame"], frame["tick"]) == (index, (index + 1) * 2), "Movement ticks left the 60 Hz host clock")
        require(frame["y"] >= -1.99 - 1e-4, "Juno went below the recovered floor response")
        if frame["floor"] & 1:
            require(frame["state"] != 3 or frame["vy"] <= 0, "Airborne frame on the floor with upward motion")
        # The original camera pushes Juno away inside 32 units and never drops below the track floor limit.
        require((frame["x"] - frame["camera_x"]) ** 2 + (frame["z"] - frame["camera_z"]) ** 2 >= 1023, "Camera inside Juno")
        require(frame["camera_y"] >= -1.99 - 100 - 1e-3, "Camera below the recovered track limit")
    clips = [frame["clip"] for frame in frames]
    for clip in (1024, 1061, 1062, 1063, 1064, 1040, 1029, 1030, 1033, 1037, 1038, 1044, 1053, 1059):
        require(clip in clips, f"Original clip {clip} missing from the movement proof")
    airborne = [i for i, f in enumerate(frames) if f["state"] == 3]
    require(airborne and trace["highest_y"] > 150 and trace["wall_ticks"] > 20, "Jump, slope or wall contact missing")
    require(frames[-1]["floor"] & 1 and frames[-1]["state"] == 0, "Juno did not settle on the ground")
    # Earlier GPU proofs stay byte-identical, including the region checkpoint.
    references = {
        "neutral.rgba": "ac70cc84b273b79b814209eae1d0451d706ac19fdf914b7345013d8e4c4bacc6",
        "cycle.rgba": json.loads((ROOT / "port/native/animation-validation.json").read_text())["animation"]["raw_stream_sha256"],
        "transitions.rgba": json.loads((ROOT / "port/native/transition-validation.json").read_text())["transition"]["raw_sha256"],
        "character.rgba": json.loads((ROOT / "port/native/character-validation.json").read_text())["character"]["raw_sha256"],
        "juno.rgba": json.loads((ROOT / "port/native/juno-selection-validation.json").read_text())["selection"]["raw_sha256"],
        "integration.rgba": json.loads((ROOT / "port/native/integration-validation.json").read_text())["integration"]["raw_sha256"],
        "region.rgba": json.loads((ROOT / "port/native/region-validation.json").read_text())["region"]["raw_sha256"],
    }
    for name, expected in references.items():
        require(digest((OUT / name).read_bytes()) == expected, "Native GPU regression: " + name)
    background = (OUT / "frame.background.rgba").read_bytes(); terrain = (OUT / "frame.terrain.rgba").read_bytes()
    actor = (OUT / "frame.character.rgba").read_bytes()
    require(all(len(p) == SIZE for p in (background, terrain, actor)), "Missing independent resource captures")
    require(background == background[:4] * (640 * 480), "Empty renderer frame is not the controlled backdrop")
    raw = (OUT / "frame.rgba").read_bytes(); require(len(raw) == FRAMES * SIZE, "Incomplete movement video stream")
    terrain_pixels, actor_pixels = changed(terrain, background), changed(actor, background)
    require(len(terrain_pixels) > 30000 and 200 < len(actor_pixels) < 60000, "Region or character failed to contribute visibly")
    coverage, hashes = [], []
    peak = max(range(FRAMES), key=lambda i: frames[i]["y"])
    first_ground = next(i for i, f in enumerate(frames) if f["floor"] & 1)
    jump = airborne[0]
    turned = next(i for i, f in enumerate(frames) if f["tick"] > 340 and f["speed04"] < -3)
    orbit = next(i for i, f in enumerate(frames) if f["tick"] > 480 and (f["camera_yaw"] - frames[239]["camera_yaw"]) % 65536 > 0x2000)
    strafe = [i for i, f in enumerate(frames) if 480 < f["tick"] <= 520 and f["lateral10"] <= -2.5 and f["keys"] == 2]
    require(len(strafe) >= 10, "C-left did not strafe Juno as the original walking state does")
    crouch = next(i for i, f in enumerate(frames) if f["state"] == 1 and f["move"] == 14)
    crouch_walk = next(i for i, f in enumerate(frames) if f["state"] == 2 and f["move"] == 4)
    roll = next(i for i, f in enumerate(frames) if f["move"] == 50) + 8
    stand = next(i for i, f in enumerate(frames) if f["tick"] > 720 and f["move"] == 8)
    slide = next(i for i, f in enumerate(frames) if f["state"] == 1 and f["move"] == 15) + 5
    require(frames[slide]["move"] == 15 and frames[roll]["move"] == 50, "Slide or roll too short for its key frame")
    aim = next(i for i, f in enumerate(frames) if f["state"] == 11) + 15
    aim_up = next(i for i, f in enumerate(frames) if f["tick"] > 960 and f["state"] == 11)
    require(frames[aim]["state"] == 11 and frames[aim_up]["state"] == 11, "Aim key frames left the aim state")
    crouch_aim = next(i for i, f in enumerate(frames) if f["state"] == 5) + 10
    require(frames[crouch_aim]["state"] == 5, "Crouched aim key frame left state 5")
    # Joint turns: 0x4840's torso twist ends every list; 0x3DB0 (standing aim) and 0x3F30 (crouched aim)
    # write five channel turns before it; walking writes none without +0x1F4.
    for f in frames:
        turns = f["joint_turns"]
        require(turns and turns[-1] == [0x44, (f["twist"] + 0x8000) % 0x10000 - 0x8000], "Torso twist entry missing")
        require(len(turns) in (1, 6) and all(t[0] in (6, 8, 0x12, 0x3C, 0x3E, 0x44) for t in turns), "Unexpected joint-turn command")
    aim_frames = [f for f in frames if f["state"] == 11]; crouch_aim_frames = [f for f in frames if f["state"] == 5]
    aim_turned = sum(len(f["joint_turns"]) == 6 and f["joint_turns"][0][0] == 6 for f in aim_frames)
    crouch_turned = sum(len(f["joint_turns"]) == 6 and f["joint_turns"][2] == [0x12, 0] for f in crouch_aim_frames)
    require(aim_turned >= len(aim_frames) - 1 and crouch_turned >= len(crouch_aim_frames) - 1, "Aim frames without joint turns")
    require(any(f["joint_turns"][1][1] != 0 for f in aim_frames) and any(f["joint_turns"][0][1] != 0 for f in aim_frames),
            "Aim did not turn the torso")
    twisted = [i for i, f in enumerate(frames) if abs(f["twist"]) > 0x400]
    require(len(twisted) > 20, "Rolls and running strafes did not twist the torso")
    twist = max(range(FRAMES), key=lambda i: abs(frames[i]["twist"]))
    # controlFadePlayer: aiming fades Juno to 255 - 4 * 0x28 = 95; elsewhere the fade runs out in 3 frames.
    opacities = [f["opacity"] for f in frames]
    require(all(o in range(95, 256) and (255 - o) % 4 == 0 for o in opacities), "Opacity outside func_80015CB8's values")
    require(all(f["opacity"] < 255 for f in aim_frames[1:] + crouch_aim_frames[1:]), "Aim frame drawn opaque")
    require(all(f["opacity"] == 255 or f["state"] in (5, 11) or opacities[i - 2] < 255 for i, f in enumerate(frames)),
            "Juno faded outside the aim")
    faded = next(i for i, o in enumerate(opacities) if o == 95)
    keys = {"landing": first_ground, "running": 60, "slope": 130, "jump": jump, "peak": peak, "turned": turned,
            "return": 215, "c-left": orbit, "crouch": crouch, "crouch-walk": crouch_walk + 10, "roll": roll, "stand": stand,
            "slide": slide, "aim": aim, "aim-up": aim_up, "crouch-aim": crouch_aim, "twist": twist, "faded": faded,
            "settled": FRAMES - 1}
    for frame in range(FRAMES):
        pixels = raw[frame * SIZE:(frame + 1) * SIZE]; hashes.append(digest(pixels))
        covered = sum(pixels[p:p + 3] != background[p:p + 3] for p in range(0, SIZE, 4))
        require(covered > 30000, "Camera lost the region")
        coverage.append(covered)
    for name, frame in keys.items():
        (OUT / f"movement-{frame:03}-{name}.png").write_bytes(png_rgb(raw[frame * SIZE:(frame + 1) * SIZE]))
    require(len(set(hashes)) > 500, "Movement sequence did not evolve")
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "rawvideo", "-pixel_format", "rgba", "-video_size", "640x480",
                    "-framerate", "30", "-i", str(OUT / "frame.rgba"), "-an", "-c:v", "libx264", "-preset", "fast",
                    "-crf", "18", "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(OUT / "forest-first-movement.mp4")], check=True)
    assets = json.loads((OUT / "movement-assets-report.json").read_text())
    camera = json.loads((OUT / "camera-assets-report.json").read_text())
    distances = [((f["x"] - f["camera_x"]) ** 2 + (f["z"] - f["camera_z"]) ** 2) ** 0.5 for f in frames]
    report = {"status": "passed", "level": 21, "geometry": 17, "frames": FRAMES, "original_frames": 1160, "unique_frames": len(set(hashes)),
              "linux_windows_checks": windows, "scenario_digest_equal_on_linux_and_windows": True,
              "final_position": trace["final"], "lowest_y": trace["lowest_y"], "highest_y": trace["highest_y"],
              "wall_contact_ticks": trace["wall_ticks"], "airborne_ticks": trace["airborne_ticks"], "move_changes": trace["move_changes"],
              "strafe_frames_at_full_speed": len(strafe),
              "joint_turns": {"aim_frames": len(aim_frames), "aim_frames_with_torso_turns": aim_turned,
                              "crouched_aim_frames": len(crouch_aim_frames), "crouched_aim_frames_with_turns": crouch_turned,
                              "frames_with_torso_twist_over_0x400": len(twisted), "largest_twist": frames[twist]["twist"]},
              "fade": {"frames_below_full_opacity": sum(o < 255 for o in opacities), "frames_at_95": opacities.count(95)},
              "clips_played": sorted(set(clips)), "key_frames": keys, "region_coverage_range": [min(coverage), max(coverage)],
              "terrain_pixels_first_frame": len(terrain_pixels), "character_pixels_in_isolation": len(actor_pixels),
              "all_seven_previous_gpu_proofs_byte_equal": True,
              "collision": {k: assets["collision"][k] for k in ("blocks", "triangles", "shared_edges", "boundary_edges", "one_sided_links",
                                                                "polylist_capacity", "edge_capacity", "sha256")},
              "physics_sha256": assets["physics"]["sha256"],
              "routine_audits": len(assets["physics"]["routine_audits"]),
              "audited_bytes": sum(a["bytes"] for a in assets["physics"]["routine_audits"]),
              "camera": {"sha256": camera["sha256"], "mode": camera["camera_mode"], "profiles": len(camera["profiles"]),
                         "routine_audits": len(camera["routine_audits"]), "level_camera_objects": camera["level_camera_objects"],
                         "audited_bytes": sum(a["bytes"] for a in camera["routine_audits"]),
                         "horizontal_distance_range": [round(min(distances), 2), round(max(distances), 2)],
                         "yaw_at_start": frames[0]["camera_yaw"], "yaw_at_end": frames[-1]["camera_yaw"]},
              "raw_sha256": digest(raw), "video_sha256": digest((OUT / "forest-first-movement.mp4").read_bytes()),
              "fps_output": 30, "duration_seconds": FRAMES / 30,
              "limits": ["Juno walking, crouch (1/2), standing and crouched aim (0xB, 5) and air states with original track collision and collision profiles; shots, water and object hit models not ported; crouched aim (5) waits for the model clip blend as the original",
                         "Original free and aim cameras (player type 0, collision mode 1); zone, spline, static and cutscene cameras not ported",
                         "Joint turns of the aim (0x3DB0, 0x3F30, 0x6290) and the torso twist (0x4840) are drawn; recoil turns need shots, which are not ported",
                         "controlFadePlayer's aim fade is drawn with the translucent render mode (Z compare, no Z update, texel alpha times opacity); the port renderer interprets materials, not display lists",
                         "A scripted controller stands in for a physical controller; raw N64 pad values pass through the original joyRead and controlReadJoypad",
                         "Native animation blend between clips; move selection, clip positions and collision profiles follow the original machine",
                         "No enemies, weapons, audio or full levelInit; no emulation; ROM and derived assets remain private"]}
    (ROOT / "port/native/movement-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: report[k] for k in ("status", "frames", "unique_frames", "highest_y", "wall_contact_ticks",
                                           "clips_played", "key_frames", "camera", "all_seven_previous_gpu_proofs_byte_equal")}))


if __name__ == "__main__":
    main()
