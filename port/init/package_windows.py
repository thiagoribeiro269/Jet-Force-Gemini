#!/usr/bin/env python3
"""Package the Windows initialization diagnostic without ROM, ELF or private paths."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[2]
README = r"""JFG: inicialização original, buffers de vídeo, DMA e dados da ROM

Exige Python x64 3.11+ e uma ROM US local (.z64), SHA-1
493ced9008dbe932d6e91179b68e8630cf23a023. A ROM não acompanha o pacote.

No PowerShell, dentro da pasta extraída:
py -3 -I -O checks_init.py --library jfg_poc.dll --manifest init-config.json --rom 'C:\caminho\baserom.us.z64' --report result.json

Esperado: sete cenários aprovados e 14 threads criadas/finalizadas.
O mainInitGame original prepara scheduler, memória, vídeo, PI e RCP, e para
explicitamente na chamada ainda não resolvida do overlay de bootstrap.
Essa parada é o limite observado, não conclusão do boot.

A rotina original de leitura da ROM usa transferências PI e mensagens de
conclusão; um arquivo é descomprimido pelo código original em buffers
separados e comparado byte a byte com zlib. O teste não usa emulador,
não produz imagem, não inicializa GPU e não altera outros processos/serviços.

Os arquivos locais necessários ficam nesta pasta. Não há ROM ou ELF no ZIP.
Documentação: https://github.com/thiagoribeiro269/Jet-Force-Gemini/tree/port/recomp-poc/port/init
Infraestrutura desenvolvida com assistência de OpenAI Codex.
"""


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build", type=Path, default=ROOT / "build/port-init")
    a = p.parse_args()
    manifest = json.loads((a.build / "proof/manifest.json").read_text())
    manifest.pop("rom_path", None); manifest.pop("elf_path", None)
    files = {"jfg_poc.dll": (a.build / "windows/jfg_poc.dll").read_bytes(),
             "checks_init.py": (ROOT / "port/init/checks_init.py").read_bytes(),
             "checks_events.py": (ROOT / "port/events/checks_events.py").read_bytes(),
             "thread_checks.py": (ROOT / "port/threads/checks.py").read_bytes(),
             "native.py": (ROOT / "port/boot/native.py").read_bytes(),
             "init-config.json": (json.dumps(manifest, indent=2) + "\n").encode(),
             "README.txt": README.encode("utf-8")}
    hashes = {name: {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()} for name,data in files.items()}
    files["hashes.json"] = (json.dumps(hashes, indent=2) + "\n").encode()
    output = a.build / "jfg-init-windows-x64.zip"
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name,data in files.items(): archive.writestr(name, data)
    print(json.dumps({"package": str(output), "size": output.stat().st_size,
                      "sha256": hashlib.sha256(output.read_bytes()).hexdigest(), "files": hashes}, indent=2))


if __name__ == "__main__": main()
