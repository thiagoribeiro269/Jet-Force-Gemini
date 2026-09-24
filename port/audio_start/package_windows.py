#!/usr/bin/env python3
"""Package Windows audio-start diagnostics without ROM or generated source."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[2]
README = r"""JFG: inicialização da thread de áudio — Windows x64

Exige Python x64 3.11+ e ROM US local, SHA-1
493ced9008dbe932d6e91179b68e8630cf23a023. A ROM não acompanha o pacote.

py -3 -I -O checks_start.py --library jfg_poc.dll --manifest start-config.json --rom 'C:\caminho\baserom.us.z64' --report start-report.json
py -3 -I -O checks_wake.py --library jfg_poc.dll --manifest start-config.json --rom 'C:\caminho\baserom.us.z64' --report wake-report.json

Esperado: três cenários de inicialização e dois de despertar aprovados. Completa amInit, inicia a thread de áudio e
para antes de amInitAudioMap. A thread registra um cliente no scheduler e
aguarda mensagens. Não processa frames ou amostras nem produz som ou imagem.
Não inicializa GPU nem modifica processos ou serviços preexistentes.

Documentação: https://github.com/thiagoribeiro269/Jet-Force-Gemini/tree/port/recomp-poc/port/audio_start
Infraestrutura desenvolvida com assistência de OpenAI Codex.
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build/port-audio-start")
    args = parser.parse_args()
    manifest = json.loads((args.build / "proof/manifest.json").read_text())
    manifest.pop("rom_path", None)
    manifest.pop("elf_path", None)
    sources = {
        "checks_start.py": "audio_start/checks_start.py",
        "checks_wake.py": "audio_start/checks_wake.py",
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
        "start-config.json": (json.dumps(manifest, indent=2) + "\n").encode(),
        "README.txt": README.encode("utf-8"),
        "THIRD_PARTY_NOTICES.md": (ROOT / "port/THIRD_PARTY_NOTICES.md").read_bytes(),
        "licenses/N64ModernRuntime-GPLv3.txt": (ROOT / "port/vendor/N64ModernRuntime/COPYING").read_bytes(),
        "licenses/N64Recomp-MIT.txt": (ROOT / "port/vendor/N64ModernRuntime/N64Recomp/LICENSE").read_bytes(),
    })
    hashes = {name: {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
              for name, data in files.items()}
    files["hashes.json"] = (json.dumps(hashes, indent=2) + "\n").encode()
    output = args.build / "jfg-audio-start-windows-x64.zip"
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in files.items():
            archive.writestr(name, data)
    print(json.dumps({"package": str(output), "size": output.stat().st_size,
                      "sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
                      "files": hashes}, indent=2))


if __name__ == "__main__":
    main()
