#!/usr/bin/env python3
"""Package the local runtime queue check without ROMs or extracted game data."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[2]
README = r"""JFG: diagnóstico das filas do N64ModernRuntime

Exige Python x64 3.11+ e ROM US local, SHA-1
493ced9008dbe932d6e91179b68e8630cf23a023. A ROM não acompanha o pacote.

No PowerShell, dentro da pasta extraída:
py -3 -I queue_checks.py --library jfg_queues.dll --rom 'C:\caminho\baserom.us.z64' --fixtures queue-fixtures.json --report result.json

As expectativas foram geradas executando as rotinas MIPS originais. Este
launcher apenas executa a biblioteca x64 e confere resultados e hashes de
memória. Não depende de emulador, não abre janela e não inicializa GPU, áudio
ou threads de jogo. Operações que precisariam suspender/reativar uma thread
retornam erro explícito; o agendamento completo ainda não foi integrado.

O teste compara o retorno da API e a RAM, excluindo os campos internos de
espera da fila e a região de pilha usada pela chamada MIPS de referência.
Não compara os registradores internos do runtime nem o tempo de execução.

Runtime original e licença: https://github.com/N64Recomp/N64ModernRuntime
Adaptador e documentação: https://github.com/thiagoribeiro269/Jet-Force-Gemini/tree/port/recomp-poc/port/runtime
Infraestrutura desenvolvida com assistência de OpenAI Codex.
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build/port-runtime")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    output = args.out or args.build / "jfg-runtime-queues-windows-x64.zip"
    files = {
        "jfg_queues.dll": (args.build / "windows/jfg_queues.dll").read_bytes(),
        "queue-fixtures.json": (args.build / "queue-fixtures.json").read_bytes(),
        "queue_checks.py": (ROOT / "port/runtime/queue_checks.py").read_bytes(),
        "README.txt": README.encode("utf-8"),
    }
    hashes = {name: {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()} for name, data in files.items()}
    files["hashes.json"] = (json.dumps(hashes, indent=2) + "\n").encode()
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in files.items():
            archive.writestr(name, data)
    print(json.dumps({"package": str(output), "size": output.stat().st_size,
                      "sha256": hashlib.sha256(output.read_bytes()).hexdigest(), "files": hashes}, indent=2))


if __name__ == "__main__":
    main()
