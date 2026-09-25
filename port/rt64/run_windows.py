#!/usr/bin/env python3
"""Run the two packaged console tests on Windows, with bounded owned processes."""
import argparse
import ctypes
from datetime import datetime, timezone
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
        raise RuntimeError("This runner requires Windows")
    work, out = args.package.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    hashes = json.loads((work / "hashes.json").read_text())
    for name, record in hashes.items():
        path = (work / name).resolve()
        if not path.is_relative_to(work) or hashlib.sha256(path.read_bytes()).hexdigest() != record["sha256"]:
            raise RuntimeError("Package hash verification failed: " + name)
    ctypes.windll.kernel32.SetErrorMode(0x8003)
    data = json.loads((work / "frame-input.json").read_text())

    def run_owned(name, exe, arguments, seconds):
        stdout, stderr = out / (name + ".stdout.log"), out / (name + ".stderr.log")
        timed_out = False
        # Real file handles avoid waiting for pipe handles inherited by error
        # reporting. Only this Popen child may be killed on its own deadline.
        with stdout.open("wb") as output, stderr.open("wb") as errors:
            child = subprocess.Popen([str(work / exe), *arguments], cwd=work,
                                     stdout=output, stderr=errors,
                                     creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                code = child.wait(timeout=seconds)
            except subprocess.TimeoutExpired:
                timed_out = True
                child.kill()
                code = child.wait(timeout=15)
        return {"exit_code": code, "timed_out": timed_out,
                "stdout": stdout.read_text(encoding="utf-8", errors="replace"),
                "stderr": stderr.read_text(encoding="utf-8", errors="replace")}

    ram = str(work / "ram.bin")
    matrix = [str(data["matrix_base"])] if data.get("matrix_base") else []
    decoder = run_owned("decoder", "jfg_rt64_decoder_test.exe",
                        [ram, str(data["list_address"]), str(data["vertex_base"]), *matrix], 30)
    render, frame = None, None
    if decoder["exit_code"] == 0 and not decoder["timed_out"]:
        render = run_owned("render", "jfg_rt64_headless.exe",
                           ["--ram", ram, "--out", str(out / "frame"), "--list", str(data["list_address"]),
                            "--vertex-base", str(data["vertex_base"]),
                            *(["--matrix-base", *matrix] if matrix else [])], 180)
        if (out / "frame.json").exists():
            frame = json.loads((out / "frame.json").read_text())
    cpu = json.loads((out / "frame.cpu.json").read_text()) if (out / "frame.cpu.json").exists() else None
    result = {"recorded_at": datetime.now(timezone.utc).isoformat(),
              "ok": bool(frame is not None and render and render["exit_code"] == 0 and not render["timed_out"]),
              "windows": platform.platform(),
              "decoder": decoder, "render": render, "frame": frame, "cpu_diagnostic": cpu}
    (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
