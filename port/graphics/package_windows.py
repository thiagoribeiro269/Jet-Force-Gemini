#!/usr/bin/env python3
"""Package Windows graphics diagnostics without ROM, assets, or generated source."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[2]
README = r"""JFG: modelo, textura e display list originais — Windows x64

Exige Python x64 3.11+ e ROM US local, SHA-1
493ced9008dbe932d6e91179b68e8630cf23a023. A ROM não acompanha o pacote.

py -3 -I -O checks_graphics.py --library jfg_poc.dll --manifest graphics-config.json --rom 'C:\caminho\baserom.us.z64' --report graphics-report.json
py -3 -I -O checks_protocol.py --library jfg_poc.dll --manifest graphics-config.json --rom 'C:\caminho\baserom.us.z64' --report protocol-report.json

Esperado: modelo 35 e textura 9097 carregados por rotinas originais e comandos de display list capturados.
O diagnóstico valida dados e comandos em RAM. Não executa RSP/RT64, não inicializa GPU nem produz imagem.
Controles são simulados; nenhum controle físico é lido. A thread de áudio aguarda mensagens, sem som.
Não modifica processos ou serviços preexistentes.

Documentação: https://github.com/thiagoribeiro269/Jet-Force-Gemini/tree/port/recomp-poc/port/graphics
Infraestrutura desenvolvida com assistência de OpenAI Codex.
"""


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build/port-graphics")
    args = parser.parse_args()
    manifest = json.loads((args.build / "proof/manifest.json").read_text())
    manifest.pop("rom_path", None)
    manifest.pop("elf_path", None)
    sources = {
        "checks_graphics.py": "graphics/checks_graphics.py",
        "graphics_asset_checks.py": "graphics/graphics_asset_checks.py",
        "checks_objects.py": "objects/checks_objects.py",
        "object_checks.py": "objects/object_checks.py",
        "checks_textures.py": "textures/checks_textures.py",
        "texture_checks.py": "textures/texture_checks.py",
        "checks_controllers.py": "controllers/checks_controllers.py",
        "controller_checks.py": "controllers/controller_checks.py",
        "checks_protocol.py": "controllers/checks_protocol.py",
        "checks_map.py": "audio_map/checks_map.py",
        "map_checks.py": "audio_map/map_checks.py",
        "checks_start.py": "audio_start/checks_start.py",
        "start_checks.py": "audio_start/start_checks.py",
        "checks_players.py": "audio_players/checks_players.py",
        "player_checks.py": "audio_players/player_checks.py",
        "checks_manager.py": "audio_manager/checks_manager.py",
        "checks_audio.py": "audio_init/checks_audio.py",
        "asset_checks.py": "audio_init/asset_checks.py",
        "checks_bootstrap.py": "bootstrap/checks_bootstrap.py",
        "checks_init.py": "init/checks_init.py",
        "checks_events.py": "events/checks_events.py",
        "thread_checks.py": "threads/checks.py",
        "native.py": "boot/native.py",
    }
    files = {name: (ROOT / "port" / path).read_bytes() for name, path in sources.items()}
    files.update({
        "jfg_poc.dll": (args.build / "windows/jfg_poc.dll").read_bytes(),
        "graphics-config.json": (json.dumps(manifest, indent=2) + "\n").encode(),
        "README.txt": README.encode("utf-8"),
        "THIRD_PARTY_NOTICES.md": (ROOT / "port/THIRD_PARTY_NOTICES.md").read_bytes(),
        "licenses/N64ModernRuntime-GPLv3.txt": (ROOT / "port/vendor/N64ModernRuntime/COPYING").read_bytes(),
        "licenses/N64Recomp-MIT.txt": (ROOT / "port/vendor/N64ModernRuntime/N64Recomp/LICENSE").read_bytes(),
    })
    hashes = {name: {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
              for name, data in files.items()}
    files["hashes.json"] = (json.dumps(hashes, indent=2) + "\n").encode()
    output = args.build / "jfg-graphics-windows-x64.zip"
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in files.items():
            archive.writestr(name, data)
    print(json.dumps({"package": str(output), "size": output.stat().st_size,
                      "sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
                      "files": hashes}, indent=2))


if __name__ == "__main__":
    main()
