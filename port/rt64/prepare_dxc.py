#!/usr/bin/env python3
"""Prepare verified Windows DXC 1.8 DLLs from RT64 Linux DXC's release.

The package is the official Microsoft DirectXShaderCompiler v1.8.2403.2
release (commit 11e1318c3). Nothing is written into the RT64 checkout.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import urllib.request
from zipfile import ZipFile


ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build/port-rt64"
CACHE = BUILD / "downloads/dxc_2024_03_29.zip"
DEST = BUILD / "dxc-runtime"
URL = ("https://github.com/microsoft/DirectXShaderCompiler/releases/download/"
       "v1.8.2403.2/dxc_2024_03_29.zip")
ZIP_SHA256 = "74874e9741f027d4321263af58d24ae0f6dde2351230680b151c87144cc0a02a"
FILES = {
    "bin/x64/dxcompiler.dll": "eb0e6196eae92f69f7e3a8a0bf0e35f30d7c2adad475bebbde3a41015ca0b414",
    "bin/x64/dxil.dll": "1f61b171315ecffabdf56d4216e77d56533e3113f8fe29c5ad34a45c52c91efa",
}
LICENSES = ("LICENSE-LLVM.txt", "LICENSE-MIT.txt", "LICENSE-MS.txt")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_atomic(path: Path, data: bytes) -> None:
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temp = Path(stream.name)
        try:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        except BaseException:
            temp.unlink(missing_ok=True)
            raise
    os.replace(temp, path)


def download() -> None:
    CACHE.parent.mkdir(parents=True, exist_ok=True)
    if CACHE.exists():
        if sha256_file(CACHE) != ZIP_SHA256:
            raise RuntimeError(f"Cached archive has unexpected SHA-256: {CACHE}")
        return
    request = urllib.request.Request(URL, headers={"User-Agent": "JFG-RT64-build/1.0"})
    with tempfile.NamedTemporaryFile(dir=CACHE.parent, delete=False) as stream:
        temp = Path(stream.name)
        try:
            digest = hashlib.sha256()
            with urllib.request.urlopen(request, timeout=60) as response:
                for chunk in iter(lambda: response.read(1024 * 1024), b""):
                    stream.write(chunk)
                    digest.update(chunk)
            stream.flush()
            os.fsync(stream.fileno())
        except BaseException:
            temp.unlink(missing_ok=True)
            raise
    if digest.hexdigest() != ZIP_SHA256:
        temp.unlink(missing_ok=True)
        raise RuntimeError("Downloaded DXC archive has unexpected SHA-256")
    os.replace(temp, CACHE)


def main() -> None:
    download()
    DEST.mkdir(parents=True, exist_ok=True)
    manifest = {
        "release": "v1.8.2403.2",
        "source_commit": "11e1318c3ed77aa08326e16eaf52598ad6430546",
        "source_url": URL,
        "archive_sha256": ZIP_SHA256,
        "files": {},
    }
    with ZipFile(CACHE) as archive:
        for name in (*FILES, *LICENSES):
            try:
                data = archive.read(name)
            except KeyError as exc:
                raise RuntimeError(f"DXC archive lacks required file: {name}") from exc
            digest = hashlib.sha256(data).hexdigest()
            if name in FILES and digest != FILES[name]:
                raise RuntimeError(f"Unexpected SHA-256 for {name}: {digest}")
            target = DEST / Path(name).name
            if not target.exists() or sha256_file(target) != digest:
                write_atomic(target, data)
            manifest["files"][target.name] = digest
    write_atomic(DEST / "manifest.json", (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode())
    print(f"Verified DXC v1.8.2403.2 runtime: {DEST}")
    for name, digest in manifest["files"].items():
        print(f"{digest}  {DEST / name}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError) as exc:
        print(f"DXC preparation failed: {exc}", file=sys.stderr)
        sys.exit(1)
