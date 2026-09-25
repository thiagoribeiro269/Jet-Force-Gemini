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
    rigged = (package / "scene.bin").read_bytes()[:8] == b"JFGNAT2\0"
    command = [str(package / "jfg_native_preview.exe"), str(package / "scene.bin")]
    render = owned("render", [*command, str(out / "frame"), *(["--animate"] if rigged else [])])
    neutral = None
    if rigged and render["exit_code"] == 0 and not render["timed_out"]:
        neutral = owned("neutral", [*command, str(out / "neutral")])
    frame = json.loads((out / "frame.json").read_text()) if (out / "frame.json").exists() else None
    result = {"ok": render["exit_code"] == 0 and not render["timed_out"] and frame is not None and
              (not rigged or (neutral and neutral["exit_code"] == 0 and not neutral["timed_out"])),
              "windows": platform.platform(), "frame": frame, "math_test": math_test, "neutral": neutral, **render}
    (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
