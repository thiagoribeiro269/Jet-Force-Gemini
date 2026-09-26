#!/usr/bin/env python3
"""Run only the owned, windowless native preview process on Windows."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if os.name != "nt":
        raise RuntimeError("Windows is required")
    package, out = args.package.resolve(), args.out.resolve()
    for name, digest in json.loads((package / "hashes.json").read_text()).items():
        path = (package / name).resolve()
        if not path.is_relative_to(package) or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise RuntimeError("Package hash mismatch")
    out.mkdir(parents=True, exist_ok=True)
    def owned(name, command):
        timed_out = False
        with (out / (name + ".stdout.log")).open("wb") as stdout, (out / (name + ".stderr.log")).open("wb") as stderr:
            child = subprocess.Popen(command, cwd=package, stdout=stdout, stderr=stderr,
                                     creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                code = child.wait(timeout=90)
            except subprocess.TimeoutExpired:
                timed_out = True
                child.kill()  # Only this runner's own child.
                code = child.wait(timeout=15)
        return {"exit_code": code, "timed_out": timed_out,
                "stdout": (out / (name + ".stdout.log")).read_text(errors="replace"),
                "stderr": (out / (name + ".stderr.log")).read_text(errors="replace")}

    math_test = owned("math", [str(package / "check_animation.exe")])
    if math_test["exit_code"] or math_test["timed_out"]:
        raise RuntimeError("Native animation mathematics failed: " + math_test["stderr"])
    controller_test = owned("controller_test", [str(package / "check_player.exe")])
    if controller_test["exit_code"] or controller_test["timed_out"]:
        raise RuntimeError("Native animation controller failed: " + controller_test["stderr"])
    character_test = owned("character_test", [str(package / "check_character.exe")])
    if character_test["exit_code"] or character_test["timed_out"]:
        raise RuntimeError("Native character controller failed: " + character_test["stderr"])
    juno = (package / "juno_sequence.txt").is_file()
    juno_test = owned("juno_selection_test", [str(package / "check_juno_selection.exe"), *([str(package / "juno-selection.bin")] if juno else [])])
    if juno_test["exit_code"] or juno_test["timed_out"]:
        raise RuntimeError("Original Juno selection failed: " + juno_test["stderr"])
    integration = (package / "jfg_native_session.exe").is_file()
    session_test = owned("session_test", [str(package / "check_session.exe"),
                         *([str(package / "scene.bin"), str(package / "juno-selection.bin")] if juno else [])])
    if session_test["exit_code"] or session_test["timed_out"]:
        raise RuntimeError("Native application session failed: " + session_test["stderr"])
    region = (package / "jfg_native_region.exe").is_file()
    region_inputs = [str(package / name) for name in ("scene.bin", "juno-selection.bin", "region-mesh.bin", "region-info.bin")]
    region_test = owned("region_test", [str(package / "check_region.exe"), *(region_inputs if region else [])])
    if region_test["exit_code"] or region_test["timed_out"]:
        raise RuntimeError("Native region/camera checks failed: " + region_test["stderr"])
    movement = (package / "jfg_native_movement.exe").is_file()
    movement_inputs = [str(package / "movement-scene.bin"), *region_inputs[1:], str(package / "collision.bin"), str(package / "juno-physics.bin"),
                       str(package / "juno-camera.bin")]
    movement_test = owned("movement_test", [str(package / "check_movement.exe"), *movement_inputs]) if movement else None
    if movement and (movement_test["exit_code"] or movement_test["timed_out"]):
        raise RuntimeError("Native movement checks failed: " + movement_test["stderr"])
    magic = (package / "scene.bin").read_bytes()[:8]
    multiple = magic == b"JFGNAT3\0"
    rigged = multiple or magic == b"JFGNAT2\0"
    character = (package / "character_sequence.txt").is_file()
    command = [str(package / "jfg_native_preview.exe"), str(package / "scene.bin")]
    options = (["--juno-selection", str(package / "juno_sequence.txt"), str(package / "juno-selection.bin")] if juno else
               ["--character", str(package / "character_sequence.txt")] if character else
               ["--sequence", str(package / "transition_sequence.txt")] if multiple else ["--animate"] if rigged else [])
    render_command = ([str(package / "jfg_native_movement.exe"), *movement_inputs, str(out / "frame")] if movement else
                      [str(package / "jfg_native_region.exe"), *region_inputs, str(out / "frame")] if region else
                      [str(package / "jfg_native_session.exe"), str(package / "scene.bin"),
                       str(package / "juno-selection.bin"), str(out / "frame")] if integration else [*command, str(out / "frame"), *options])
    render = owned("render", render_command)
    neutral = None
    cycle = None
    wide_cycle = None
    transitions = None
    character_regression = None
    juno_regression = None
    integration_regression = None
    region_regression = None
    if rigged and render["exit_code"] == 0 and not render["timed_out"]:
        if multiple:
            region_regression = owned("region", [str(package / "jfg_native_region.exe"), *region_inputs, str(out / "region")]) if movement else None
            if region:
                integration_regression = owned("integration", [str(package / "jfg_native_session.exe"), str(package / "scene.bin"),
                                                               str(package / "juno-selection.bin"), str(out / "integration")])
            if integration:
                juno_regression = owned("juno", [*command, str(out / "juno"), "--juno-selection",
                                                str(package / "juno_sequence.txt"), str(package / "juno-selection.bin")])
            if juno:
                character_regression = owned("character", [*command, str(out / "character"), "--character", str(package / "character_sequence.txt")])
            if character:
                transitions = owned("transitions", [*command, str(out / "transitions"), "--sequence", str(package / "transition_sequence.txt")])
            cycle = owned("cycle", [*command, str(out / "cycle"), "--animate"])
            wide_cycle = owned("cycle_wide", [*command, str(out / "cycle-wide"), "--animate-wide"])
        neutral = owned("neutral", [*command, str(out / "neutral")])
    frame = json.loads((out / "frame.json").read_text()) if (out / "frame.json").exists() else None
    result = {"ok": render["exit_code"] == 0 and not render["timed_out"] and frame is not None and
              (not rigged or (neutral and neutral["exit_code"] == 0 and not neutral["timed_out"])) and
              (not multiple or (cycle and cycle["exit_code"] == 0 and not cycle["timed_out"] and
                                wide_cycle and wide_cycle["exit_code"] == 0 and not wide_cycle["timed_out"])),
              "windows": platform.platform(), "frame": frame, "math_test": math_test, "controller_test": controller_test,
              "neutral": neutral, "cycle_regression": cycle, "wide_cycle": wide_cycle,
              "controller_trace": json.loads((out / "frame.controller.json").read_text()) if (out / "frame.controller.json").exists() else None,
              **render}
    result["character_test"] = character_test
    result["juno_selection_test"] = juno_test
    result["session_test"] = session_test
    session_path = out / ("integration.session.json" if region else "frame.session.json")
    result["session"] = json.loads(session_path.read_text()) if integration and session_path.exists() else None
    result["region_test"] = region_test
    region_trace_path = out / ("region.region.json" if movement else "frame.region.json")
    result["region_trace"] = json.loads(region_trace_path.read_text()) if region and region_trace_path.exists() else None
    result["movement_test"] = movement_test
    result["movement_trace"] = json.loads((out / "frame.movement.json").read_text()) if movement and (out / "frame.movement.json").exists() else None
    result["region_regression"] = region_regression
    result["integration_regression"] = integration_regression
    result["juno_selection_regression"] = juno_regression
    result["transition_regression"] = transitions
    result["character_regression"] = character_regression
    result["ok"] = bool(result["ok"] and (not character or (transitions and transitions["exit_code"] == 0 and not transitions["timed_out"])))
    result["ok"] = bool(result["ok"] and (not juno or (character_regression and character_regression["exit_code"] == 0 and not character_regression["timed_out"])))
    result["ok"] = bool(result["ok"] and (not integration or (result["session"] and juno_regression and juno_regression["exit_code"] == 0 and not juno_regression["timed_out"])))
    result["ok"] = bool(result["ok"] and (not region or (result["region_trace"] and integration_regression and integration_regression["exit_code"] == 0 and not integration_regression["timed_out"])))
    result["ok"] = bool(result["ok"] and (not movement or (result["movement_trace"] and region_regression and region_regression["exit_code"] == 0 and not region_regression["timed_out"])))
    (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
