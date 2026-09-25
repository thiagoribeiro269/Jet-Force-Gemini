# Inicialização de texturas e modelos

Este perfil continua `mainInitRlo` depois do mapa de áudio e dos controles. Ele
executa as rotinas originais `texInitTextures` (`+0x40`) e `modInitModels`
(`+0x50`), com as chamadas existentes de `mainPreNMI` em `+0x48` e `+0x58`.
A execução para antes de `objInitObjects`, chamado em `+0x60`: o `runLink`
carrega o overlay 34 e corrige o `JAL` para o endereço real: `0x801B6D50`
nos cenários NTSC/MPAL e `0x801BE550` no cenário PAL, todos com a mesma ROM US.
O manifesto seleciona 152 funções originais recompiladas, 26 imports de
plataforma e 50 fronteiras adiadas em 12 seções.

As rotinas inicializam diretórios, caches e áreas de trabalho. A conferência
encontrou 657 entradas na tabela principal de texturas, 6.663 na tabela
alternativa selecionada pelo bit `0x8000` do ID, 107 sprites e 904 modelos.
São 11 buffers alocados e quatro tabelas de offsets carregadas da ROM. O
teste compara os diretórios em RAM com seus bytes originais, os tamanhos,
endereços e separação das alocações, os contadores, os caches vazios e os
cursores da área de trabalho dos modelos. Ele não decodifica uma textura,
sprite ou modelo individual nem produz imagem.

O backend de controles representa quatro portas ausentes. O oráculo MIPS
observa a fronteira antes da entrega controlada do evento SI; a verificação
nativa chama o pump e confere o estado depois da entrega. Nenhum controle
físico é acessado. A thread de áudio continua aguardando mensagens, sem
processar frames ou amostras.

## Evidência e limites

O runner completo passou no Linux x86-64: três cenários nativos de TV, oito
casos do protocolo de controles, três comparações com a execução MIPS e o
smoke test. As comparações MIPS observaram três traps previstos, incluindo
o `JAL` corrigido para `objInitObjects`, além de leituras da ROM, escritas AI,
16 GPRs restantes e RAM sob as máscaras de estruturas privadas já usadas
pelos perfis anteriores. A diferença conhecida de `a0` é conferida com os
dois valores exatos em cada caso: `mainPreNMI` deixa `a0=1` no MIPS após
consultar `resetMsgQueue`, enquanto o import nativo preserva o endereço
da fila na fronteira final. Os relatórios estão em
`build/port-textures/native-report.json`,
`build/port-textures/protocol-report.json` e
`build/port-textures/mips-report.json`.

Este perfil não executa `objInitObjects`, não renderiza, não produz áudio e
não é um jogo executável. As APIs de controle do oráculo usam contratos
explícitos para portas ausentes; PIF DMA, temporização física e estado
interno de SI não são emulados. Não há comparação geral de FPR/FCSR.
Os três cenários nativos e os oito testes de protocolo também passaram no
Windows x64 da RTX, por console SSH, com Python 3.12.10 usando `-I -O`.
Os hashes do pacote e de cada arquivo foram conferidos; a saída foi zero.
A [evidência estruturada](validation.json) identifica esses artefatos e os
limites da comparação. A regressão do perfil anterior de controles passou
no Linux, incluindo a referência MIPS. A compilação N64 e a comparação
binária com a ROM US também passaram.

## Reproduzir

Com a ROM US local e as dependências de `port/poc/requirements.txt`:

```sh
build/port-recomp/.venv/bin/python port/textures/run.py
```

O runner prepara `build/port-textures/proof`, recompila as funções selecionadas,
compila a biblioteca de diagnóstico com `JFG_CONTROLLER_PROFILE=ON` e executa
os verificadores nativo, de protocolo e MIPS. Para compilar a DLL Windows x64
com o perfil gerado:

```sh
cmake -S port/poc -B build/port-textures/windows -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DJFG_BOOT_PROFILE=ON -DJFG_THREAD_PROFILE=ON \
  -DJFG_EVENT_PROFILE=ON -DJFG_INIT_PROFILE=ON -DJFG_AUDIO_MANAGER_PROFILE=ON \
  -DJFG_CONTROLLER_PROFILE=ON \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/port/poc/windows-llvm-mingw.cmake" \
  -DJFG_LLVM_MINGW_ROOT="$PWD/build/port-recomp/cross-toolchain/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64" \
  -DJFG_PROOF_DIR="$PWD/build/port-textures/proof"
cmake --build build/port-textures/windows --parallel 2
python3 port/textures/package_windows.py
```

O pacote contém a DLL, o perfil e os verificadores nativos, inclusive os da
cadeia de áudio e controles. A comparação MIPS exige o ELF e fica fora do
pacote. ROM, assets extraídos, código C gerado e binários do jogo permanecem
em caminhos locais ignorados pelo Git; os artefatos deste perfil ficam em
`build/`. A ROM US deve ser fornecida localmente.

Antes de uma distribuição pública, ainda é preciso resolver o licenciamento
do código herdado e cumprir os avisos das dependências; consulte
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md). Infraestrutura
desenvolvida com assistência de OpenAI Codex.
