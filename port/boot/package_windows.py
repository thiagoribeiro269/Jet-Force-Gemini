#!/usr/bin/env python3
"""Package a locally built Windows diagnostic without ROMs or private paths."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[2]
README = """JFG: diagnóstico de inicialização da memória e do carregador

Este pacote local não é um jogo executável completo. Não abre janela, não usa
GPU e não reproduz áudio. Exige Python x64 3.11+ e uma ROM US local (.z64),
SHA-1 493ced9008dbe932d6e91179b68e8630cf23a023. A ROM não acompanha o pacote.

No PowerShell, dentro da pasta extraída:

py -3 .\\native.py --library .\\jfg_poc.dll --manifest .\\boot-config.json --rom 'C:\\caminho\\baserom.us.z64' --report .\\result.json

O resultado esperado contém status=passed, quatro módulos e 158 posições na
tabela do carregador. --extended seleciona o limite de heap de 6 MiB, em vez
de 4 MiB. O launcher usa apenas a biblioteca padrão do Python; o código do
jogo é executado na DLL x64, sem emulador MIPS.

jfg_poc_smoke.exe faz somente quatro verificações simples de CPU sem ROM;
ele não comprova a sequência completa de inicialização acima.

As rotinas originais de heap e runLink são recompiladas. As três interfaces
de plataforma são leitura síncrona da ROM e dois contratos de coerência de
cache. Dois caminhos que parariam sons ativos ainda retornam erro controlado.

Este artefato é gerado para validação local; compilação não comprova execução
no Windows. A evidência de execução deve vir de result.json nessa máquina.
Créditos e limites: https://github.com/thiagoribeiro269/Jet-Force-Gemini/tree/port/recomp-poc/port
Infraestrutura desenvolvida com assistência de OpenAI Codex.
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=ROOT / "build/port-boot/proof/manifest.json")
    parser.add_argument("--binaries", type=Path, default=ROOT / "build/port-boot/windows")
    parser.add_argument("--out", type=Path, default=ROOT / "build/port-boot/jfg-heap-linker-windows-x64.zip")
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    contents = {name: (args.binaries / name).read_bytes() for name in ("jfg_poc.dll", "jfg_poc_smoke.exe")}
    config = {
        "rom_sha1": manifest["rom_sha1"],
        "library_sha256": hashlib.sha256(contents["jfg_poc.dll"]).hexdigest(),
        "functions": [{"name": f["name"]} for f in manifest["functions"]],
        "sections": [{k: s[k] for k in ("index", "overlay", "vram", "load_size")} for s in manifest["sections"]],
        "reference_symbols": {k: manifest["reference_symbols"][k] for k in
                              ("overlayTable", "overlayCount", "mmExtendedRam", "mmEndRam")},
        "boot_profile": manifest["boot_profile"],
    }
    contents["boot-config.json"] = (json.dumps(config, indent=2) + "\n").encode()
    contents["native.py"] = (ROOT / "port/boot/native.py").read_bytes()
    contents["README.txt"] = README.encode("utf-8")
    hashes = {name: {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()} for name, data in contents.items()}
    contents["hashes.json"] = (json.dumps(hashes, indent=2) + "\n").encode()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.out, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in contents.items():
            archive.writestr(name, data)
    print(json.dumps({"package": str(args.out), "size": args.out.stat().st_size,
                      "sha256": hashlib.sha256(args.out.read_bytes()).hexdigest(), "files": hashes}, indent=2))


if __name__ == "__main__":
    main()
