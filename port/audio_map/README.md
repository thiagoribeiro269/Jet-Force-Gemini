# Inicialização do mapa de áudio

Esta etapa executa `amInitAudioMap`, `amGetSfxSettings` e `amResetAudioMap`
originais após o início da thread de áudio. `mainInitRlo` chama o mapa pelo
overlay 36 em `+0x2C` e para na chamada seguinte, `joyInit`, em `+0x34`.
O perfil recompila 148 funções originais; as demais dependências selecionadas
são imports da plataforma ou fronteiras explícitas.

O mapa aloca `0x5A0` bytes para 40 slots de som de `0x24` bytes cada e dois
vetores de `0xA0` bytes para 40 ponteiros. A reinicialização deixa o índice
da pilha livre em 39, nenhum ponto ativo e limpa 12 handles de ambiente.
O verificador confere os três endereços de alocação, a lista livre, os campos
zerados e os símbolos globais usados por essas rotinas. A thread de áudio
permanece bloqueada na fila do scheduler; nenhum frame ou amostra é processado.

## Evidência e limites

O runner completo passou no Linux x86-64: três cenários nativos para os
parâmetros de TV 1, 0 e 2, três comparações com a execução MIPS original e
o smoke test da biblioteca. Os relatórios locais estão em
`build/port-audio-map/native-report.json` e
`build/port-audio-map/mips-report.json`. O oráculo comparou 17 GPRs, leituras
da ROM, escritas da interface AI e RAM sob as máscaras de estruturas privadas
já estabelecidas; `a0` coincidiu nesta fronteira. Cada cenário encerrou as
três threads da sessão.

Os três cenários também passaram no Windows x64 da RTX, por console SSH,
com Python 3.12.10 usando `-I -O`, hashes conferidos e saída zero. A
[evidência estruturada](validation.json) identifica os artefatos e distingue
os testes nativos da comparação MIPS feita no Linux.

`joyInit` continua uma fronteira de falha controlada. O perfil não inicializa
controles, não produz som ou imagem e ainda não executa um jogo. Não há
comparação geral de FPR/FCSR, temporização física nem síntese de áudio.

## Reproduzir

Com a ROM US local e as dependências Python de `port/poc/requirements.txt`:

```sh
build/port-recomp/.venv/bin/python port/audio_map/run.py
```

O comando gera `build/port-audio-map/proof`, recompila com
`build/port-bootstrap/jfg-toolchain/jfg_recomp_tool`, compila o teste nativo
Linux e grava os relatórios. Para compilar o DLL de diagnóstico Windows x64
usando o perfil já gerado:

```sh
cmake -S port/poc -B build/port-audio-map/windows -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DJFG_BOOT_PROFILE=ON -DJFG_THREAD_PROFILE=ON \
  -DJFG_EVENT_PROFILE=ON -DJFG_INIT_PROFILE=ON -DJFG_AUDIO_MANAGER_PROFILE=ON \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/port/poc/windows-llvm-mingw.cmake" \
  -DJFG_LLVM_MINGW_ROOT="$PWD/build/port-recomp/cross-toolchain/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64" \
  -DJFG_PROOF_DIR="$PWD/build/port-audio-map/proof"
cmake --build build/port-audio-map/windows --parallel 2
python3 port/audio_map/package_windows.py
```

O pacote contém os verificadores, a DLL e o perfil, sem ROM, ELF, assets ou
código gerado. A ROM US é fornecida localmente. O próximo perfil,
[controles](../controllers/README.md), já avança por `joyInit` usando quatro
portas ausentes como condição explícita de teste.

Infraestrutura desenvolvida com assistência de OpenAI Codex.
