#!/usr/bin/env python3
"""Package the native preview and private converted assets; never publish ZIP."""
import hashlib
import argparse
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--out", type=Path, default=ROOT / "build/port-native")
out = parser.parse_args().out.resolve()
if not out.is_relative_to(ROOT / "build"):
    raise ValueError("Private package must remain under ignored build/")
build = ROOT / "build/port-native"
files = {name: (build / name).read_bytes() for name in ("jfg_native_preview.exe", "check_animation.exe", "check_player.exe", "check_character.exe", "check_juno_selection.exe")}
files["scene.bin"] = (out / "scene.bin").read_bytes()
assets = json.loads((out / "assets-report.json").read_text())
if assets["scene_sha256"] != hashlib.sha256(files["scene.bin"]).hexdigest():
    raise ValueError("Scene differs from its preparation report")
if files["scene.bin"][:8] == b"JFGNAT3\0":
    files["transition_sequence.txt"] = (ROOT / "port/native/transition_sequence.txt").read_bytes()
if assets.get("character_profile") or assets.get("juno_selection_profile"):
    files["character_sequence.txt"] = (ROOT / "port/native/character_sequence.txt").read_bytes()
if assets.get("juno_selection_profile"):
    files["juno_sequence.txt"] = (ROOT / "port/native/juno_sequence.txt").read_bytes()
    files["juno-selection.bin"] = (out / "juno-selection.bin").read_bytes()
    selection = json.loads((out / "selection-assets-report.json").read_text())
    if hashlib.sha256(files["juno-selection.bin"]).hexdigest() != selection["payload_sha256"]:
        raise ValueError("Juno selection differs from its static audit")
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
