#!/usr/bin/env python3
"""Package the native event diagnostic without ROMs, ELF or private paths."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[2]
README = r"""JFG: scheduler original, eventos e temporizadores

Exige Python x64 3.11+ e ROM US local (.z64), SHA-1
493ced9008dbe932d6e91179b68e8630cf23a023. A ROM não acompanha o pacote.

No PowerShell, dentro da pasta extraída:
py -3 -I -O checks_events.py --library jfg_poc.dll --manifest event-config.json --rom 'C:\caminho\baserom.us.z64' --report result.json

Esperado: 15 cenários aprovados; todas as nove threads criadas são juntadas.
O scheduler original inicia antes do heap, recebe pulsos de VI e encaminha
mensagens aos clientes. Temporizadores acordam threads suspensas.

O teste usa relógio controlado e relógio monotônico real do computador.
Não usa emulador, não inicia threads de timer em segundo plano e cancela
seus temporizadores antes de fechar. Não altera serviços ou outros processos.
PRENMI é apenas um evento convidado; não solicita desligamento do Windows.

O VI é um serviço sem renderização. Não há imagem, processamento RSP/RDP,
áudio, boot completo ou partida jogável. Caminhos gráficos não implementados
retornam erro explícito. Expectativas MIPS são verificadas separadamente.

Código e limites: https://github.com/thiagoribeiro269/Jet-Force-Gemini/tree/port/recomp-poc/port/events
Infraestrutura desenvolvida com assistência de OpenAI Codex.
"""


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build", type=Path, default=ROOT / "build/port-events")
    a = p.parse_args()
    manifest = json.loads((a.build / "proof/manifest.json").read_text())
    manifest.pop("elf_path", None)
    manifest.pop("rom_path", None)
    files = {"jfg_poc.dll": (a.build / "windows/jfg_poc.dll").read_bytes(),
             "checks_events.py": (ROOT / "port/events/checks_events.py").read_bytes(),
             "thread_checks.py": (ROOT / "port/threads/checks.py").read_bytes(),
             "native.py": (ROOT / "port/boot/native.py").read_bytes(),
             "event-config.json": (json.dumps(manifest, indent=2) + "\n").encode(),
             "README.txt": README.encode("utf-8")}
    hashes = {name: {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()} for name, data in files.items()}
    files["hashes.json"] = (json.dumps(hashes, indent=2) + "\n").encode()
    output = a.build / "jfg-events-windows-x64.zip"
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in files.items(): archive.writestr(name, data)
    print(json.dumps({"package": str(output), "size": output.stat().st_size,
                      "sha256": hashlib.sha256(output.read_bytes()).hexdigest(), "files": hashes}, indent=2))


if __name__ == "__main__": main()
