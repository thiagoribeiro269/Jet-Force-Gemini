#!/usr/bin/env python3
"""Package the native preview and private converted assets; never publish ZIP."""
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[2]
out = ROOT / "build/port-native"
files = {name: (out / name).read_bytes() for name in ("jfg_native_preview.exe", "scene.bin")}
files["run_windows.py"] = (ROOT / "port/native/run_windows.py").read_bytes()
files["PRIVATE.txt"] = b"Private game-derived meshes and textures. Do not publish this package.\n"
files["hashes.json"] = (json.dumps({n: hashlib.sha256(v).hexdigest() for n, v in files.items()}, indent=2) + "\n").encode()
package = out / "native-private.zip"
with zipfile.ZipFile(package, "w", compression=zipfile.ZIP_DEFLATED) as archive:
    for name, data in files.items():
        archive.writestr(name, data)
report = {"private": True, "package": str(package), "sha256": hashlib.sha256(package.read_bytes()).hexdigest(),
          "bytes": package.stat().st_size, "contains_game_assets": True, "contains_rom": False}
(out / "package-report.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report))
