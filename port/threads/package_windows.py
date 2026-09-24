#!/usr/bin/env python3
"""Package the native thread diagnostic without ROM, ELF or private paths."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[2]
README = r"""JFG: diagnóstico de threads e mensagens

Exige Python x64 3.11+ e a ROM US local (.z64):
SHA-1 493ced9008dbe932d6e91179b68e8630cf23a023.
A ROM não acompanha o pacote. Extraia todos os arquivos na mesma pasta.

No PowerShell:
py -3 -I -O checks.py --library jfg_poc.dll --manifest thread-config.json --rom 'C:\caminho\baserom.us.z64' --report result.json

O teste cria threads somente dentro do próprio processo, suspende/retoma
rotinas do jogo por mensagens e junta todas as threads antes de encerrar.
Não abre janela, não usa emulador, não inicializa GPU e não altera serviços.
As mensagens de conclusão DP/blur/refração são entradas controladas do teste;
não representam trabalho gráfico executado. Ainda não há boot completo.

Esperado: 12 cenários aprovados, com 21 threads criadas e finalizadas.
Os testes diferenciais contra MIPS são executados separadamente no Linux.

Código, créditos e limites:
https://github.com/thiagoribeiro269/Jet-Force-Gemini/tree/port/recomp-poc/port/threads
Infraestrutura desenvolvida com assistência de OpenAI Codex.
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build/port-threads")
    args = parser.parse_args()
    manifest = json.loads((args.build / "proof/manifest.json").read_text())
    manifest.pop("rom_path", None)
    manifest.pop("elf_path", None)
    output = args.build / "jfg-threads-windows-x64.zip"
    files = {"jfg_poc.dll": (args.build / "windows/jfg_poc.dll").read_bytes(),
             "checks.py": (ROOT / "port/threads/checks.py").read_bytes(),
             "native.py": (ROOT / "port/boot/native.py").read_bytes(),
             "thread-config.json": (json.dumps(manifest, indent=2) + "\n").encode(),
             "README.txt": README.encode("utf-8")}
    hashes = {name: {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()} for name, data in files.items()}
    files["hashes.json"] = (json.dumps(hashes, indent=2) + "\n").encode()
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in files.items(): archive.writestr(name, data)
    print(json.dumps({"package": str(output), "size": output.stat().st_size,
                      "sha256": hashlib.sha256(output.read_bytes()).hexdigest(), "files": hashes}, indent=2))


if __name__ == "__main__":
    main()
