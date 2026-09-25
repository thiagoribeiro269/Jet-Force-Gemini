#!/usr/bin/env python3
"""Package a private one-shot RT64 Windows diagnostic, including its RAM fixture."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/port-rt64"
RT64 = ROOT / "build/port-graphics/rt64-source"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=OUT)
    args = parser.parse_args()
    out = args.out.resolve()
    if not out.is_relative_to(ROOT / "build"):
        raise ValueError("Private packages must remain under ignored build/")
    frame = json.loads((out / "frame-input.json").read_text())
    ram = Path(frame["ram"]).read_bytes()
    if hashlib.sha256(ram).hexdigest() != frame["ram_sha256"]:
        raise ValueError("Private input changed after preparation")
    build = OUT / "windows-x64"
    sources = {
        "jfg_rt64_headless.exe": build / "jfg_rt64_headless.exe",
        "jfg_rt64_decoder_test.exe": build / "jfg_rt64_decoder_test.exe",
        "run_windows.py": ROOT / "port/rt64/run_windows.py",
        "dxcompiler.dll": OUT / "dxc-runtime/dxcompiler.dll",
        "dxil.dll": OUT / "dxc-runtime/dxil.dll",
        "dxc-runtime-manifest.json": OUT / "dxc-runtime/manifest.json",
        "SDL2.dll": RT64 / "src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64/SDL2.dll",
        "licenses/RT64-MIT.txt": RT64 / "LICENSE",
        "licenses/Plume-MIT.txt": RT64 / "src/contrib/plume/LICENSE",
    }
    files = {name: path.read_bytes() for name, path in sources.items()}
    for path in (OUT / "dxc-runtime").glob("LICENSE*"):
        files["licenses/" + path.name] = path.read_bytes()
    public_frame = {key: value for key, value in frame.items() if key != "ram"}
    files["frame-input.json"] = (json.dumps(public_frame, indent=2) + "\n").encode()
    files["ram.bin"] = ram
    files["PRIVATE.txt"] = (
        "Private diagnostic fixture. Contains game-derived RAM; never publish this archive.\n"
        "Runs one controlled RT64 D3D12 framebuffer without window, swapchain or VI.\n"
        "No existing processes or services are stopped by these executables.\n"
    ).encode()
    hashes = {name: {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
              for name, data in files.items()}
    files["hashes.json"] = (json.dumps(hashes, indent=2) + "\n").encode()
    package = out / "jfg-rt64-private-windows-x64.zip"
    with zipfile.ZipFile(package, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in files.items(): archive.writestr(name, data)
    report = {"package": str(package), "private": True, "contains_game_ram": True,
              "size": package.stat().st_size, "sha256": hashlib.sha256(package.read_bytes()).hexdigest(),
              "files": hashes}
    (out / "package-report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: report[k] for k in ("package", "private", "size", "sha256")}))


if __name__ == "__main__":
    main()
