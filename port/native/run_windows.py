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
    timed_out = False
    with (out / "stdout.log").open("wb") as stdout, (out / "stderr.log").open("wb") as stderr:
        child = subprocess.Popen([str(package / "jfg_native_preview.exe"), str(package / "scene.bin"), str(out / "frame")],
                                 cwd=package, stdout=stdout, stderr=stderr, creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            code = child.wait(timeout=90)
        except subprocess.TimeoutExpired:
            timed_out = True
            child.kill()  # Only the process created immediately above.
            code = child.wait(timeout=15)
    frame = json.loads((out / "frame.json").read_text()) if (out / "frame.json").exists() else None
    result = {"ok": code == 0 and not timed_out and frame is not None, "exit_code": code,
              "timed_out": timed_out, "windows": platform.platform(), "frame": frame,
              "stdout": (out / "stdout.log").read_text(errors="replace"),
              "stderr": (out / "stderr.log").read_text(errors="replace")}
    (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
