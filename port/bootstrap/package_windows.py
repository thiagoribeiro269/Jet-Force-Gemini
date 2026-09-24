#!/usr/bin/env python3
"""Package the native dynamic-linker diagnostic, without ROM or generated source."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[2]
README = r"""JFG: carregador dinâmico original — diagnóstico Windows x64

Exige Python x64 3.11+ e ROM US fornecida localmente (.z64), SHA-1
493ced9008dbe932d6e91179b68e8630cf23a023. A ROM não acompanha o pacote.

No PowerShell, dentro desta pasta:
py -3 -I -O checks_bootstrap.py --library jfg_poc.dll --manifest bootstrap-config.json --rom 'C:\caminho\baserom.us.z64' --report bootstrap-report.json
py -3 -I -O checks_linker.py --library jfg_poc.dll --manifest bootstrap-config.json --rom 'C:\caminho\baserom.us.z64' --report linker-report.json

O código original carrega mainInitRlo (overlay 36), executa seu início,
carrega o overlay 25 e para explicitamente antes de amInit, ainda não portada.
O boot não está completo. Não há imagem, áudio ou inicialização da GPU.
Os testes encerram somente suas próprias threads e não alteram serviços.

Documentação: https://github.com/thiagoribeiro269/Jet-Force-Gemini/tree/port/recomp-poc/port/bootstrap
Infraestrutura desenvolvida com assistência de OpenAI Codex.
"""


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build", type=Path, default=ROOT / "build/port-bootstrap")
    a = p.parse_args()
    manifest = json.loads((a.build / "proof/manifest.json").read_text())
    manifest.pop("rom_path", None); manifest.pop("elf_path", None)
    sources = {"checks_bootstrap.py": "bootstrap/checks_bootstrap.py", "checks_linker.py": "bootstrap/checks_linker.py",
               "checks_init.py": "init/checks_init.py", "checks_events.py": "events/checks_events.py",
               "thread_checks.py": "threads/checks.py", "native.py": "boot/native.py"}
    files = {name: (ROOT / "port" / path).read_bytes() for name, path in sources.items()}
    files.update({"jfg_poc.dll": (a.build / "windows/jfg_poc.dll").read_bytes(),
                  "bootstrap-config.json": (json.dumps(manifest, indent=2) + "\n").encode(),
                  "README.txt": README.encode("utf-8")})
    hashes = {name: {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()} for name,data in files.items()}
    files["hashes.json"] = (json.dumps(hashes, indent=2) + "\n").encode()
    output = a.build / "jfg-bootstrap-windows-x64.zip"
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name,data in files.items(): archive.writestr(name, data)
    print(json.dumps({"package": str(output), "size": output.stat().st_size,
                      "sha256": hashlib.sha256(output.read_bytes()).hexdigest(), "files": hashes}, indent=2))


if __name__ == "__main__": main()
