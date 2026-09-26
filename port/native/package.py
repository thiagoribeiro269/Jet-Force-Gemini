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
files = {name: (build / name).read_bytes() for name in ("jfg_native_preview.exe", "check_animation.exe", "check_player.exe", "check_character.exe", "check_juno_selection.exe", "check_session.exe", "check_region.exe")}
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
if assets.get("integration_profile"):
    files["jfg_native_session.exe"] = (build / "jfg_native_session.exe").read_bytes()
if assets.get("region_profile"):
    files["jfg_native_region.exe"] = (build / "jfg_native_region.exe").read_bytes()
    region = json.loads((out / "region-assets-report.json").read_text())
    for name, key in (("region-mesh.bin", "mesh_sha256"), ("region-info.bin", "region_info_sha256")):
        files[name] = (out / name).read_bytes()
        if hashlib.sha256(files[name]).hexdigest() != region[key]:
            raise ValueError("Native region differs from its conversion report")
if assets.get("movement_profile"):
    files["jfg_native_movement.exe"] = (build / "jfg_native_movement.exe").read_bytes()
    files["check_movement.exe"] = (build / "check_movement.exe").read_bytes()
    files["movement-scene.bin"] = (out / "movement-scene.bin").read_bytes()
    if hashlib.sha256(files["movement-scene.bin"]).hexdigest() != assets["movement_scene_sha256"]:
        raise ValueError("Movement scene differs from its preparation report")
    movement = json.loads((out / "movement-assets-report.json").read_text())
    for name, key in (("collision.bin", "collision"), ("juno-physics.bin", "physics")):
        files[name] = (out / name).read_bytes()
        if hashlib.sha256(files[name]).hexdigest() != movement[key]["sha256"]:
            raise ValueError("Native movement data differs from its conversion report")
    files["juno-camera.bin"] = (out / "juno-camera.bin").read_bytes()
    camera = json.loads((out / "camera-assets-report.json").read_text())
    if hashlib.sha256(files["juno-camera.bin"]).hexdigest() != camera["sha256"]:
        raise ValueError("Native camera data differs from its conversion report")
    # Play package: the windowed executable, its replay tool and tests.
    for name in ("jfg_native_play.exe", "jfg_native_replay.exe", "check_play.exe"):
        files[name] = (build / name).read_bytes()
    files["controles.ini"] = (ROOT / "port/native/controles.ini").read_bytes()
    files["LEIA-ME.txt"] = (ROOT / "port/native/LEIA-ME-jogar.txt").read_bytes()
files["run_windows.py"] = (ROOT / "port/native/run_windows.py").read_bytes()
files["PRIVATE.txt"] = b"Private game-derived meshes and textures. Do not publish this package.\n"
files["hashes.json"] = (json.dumps({n: hashlib.sha256(v).hexdigest() for n, v in files.items()}, indent=2) + "\n").encode()
package = out / "native-private.zip"
with zipfile.ZipFile(package, "w", compression=zipfile.ZIP_DEFLATED) as archive:
    for name, data in files.items():
        archive.writestr(name, data)
report = {"private": True, "package": str(package), "sha256": hashlib.sha256(package.read_bytes()).hexdigest(),
          "bytes": package.stat().st_size, "contains_game_assets": True, "contains_rom": False}
if "jfg_native_play.exe" in files:
    # Folder for Thiago to play: the game, the replay tool and the data only.
    names = ["jfg_native_play.exe", "jfg_native_replay.exe", "controles.ini", "LEIA-ME.txt", "movement-scene.bin",
             "juno-selection.bin", "region-mesh.bin", "region-info.bin", "collision.bin", "juno-physics.bin", "juno-camera.bin"]
    play = {name: files[name] for name in names}
    play["hashes.json"] = (json.dumps({n: hashlib.sha256(v).hexdigest() for n, v in play.items()}, indent=2) + "\n").encode()
    play_package = out / "jogar.zip"
    with zipfile.ZipFile(play_package, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in play.items():
            archive.writestr(name, data)
    report["play_package"] = str(play_package)
    report["play_sha256"] = hashlib.sha256(play_package.read_bytes()).hexdigest()
(out / "package-report.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report))
