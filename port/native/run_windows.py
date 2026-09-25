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
    magic = (package / "scene.bin").read_bytes()[:8]
    multiple = magic == b"JFGNAT3\0"
    rigged = multiple or magic == b"JFGNAT2\0"
    command = [str(package / "jfg_native_preview.exe"), str(package / "scene.bin")]
    options = ["--sequence", str(package / "transition_sequence.txt")] if multiple else ["--animate"] if rigged else []
    render = owned("render", [*command, str(out / "frame"), *options])
    neutral = None
    cycle = None
    wide_cycle = None
    if rigged and render["exit_code"] == 0 and not render["timed_out"]:
        if multiple:
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
    (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
