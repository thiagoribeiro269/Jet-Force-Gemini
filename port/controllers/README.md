# Inicialização de controles sem dispositivo físico

Esta etapa continua `mainInitRlo` após o mapa de áudio e executa `joyInit`
e `joyResetMap` originais. A chamada de `joyInit` fica no overlay 36 em
`+0x34`; a execução para antes de `texInitTextures`, em `+0x40`. O perfil
recompila 150 funções originais e mantém a próxima rotina gráfica como
fronteira de falha controlada.

O backend de teste fornece a `osContInit` uma condição explícita de quatro
portas sem controle. `joyInit` cria a fila, registra o evento SI, inicia uma
leitura lógica e retorna `-1` porque a porta principal está ausente. O
evento de conclusão SI só entra na fila quando o teste chama o pump de modo
controlado; o relatório confere o estado antes e depois dessa entrega.
Nenhum controle físico, botão ou dispositivo privado é acessado.

O protocolo local cobre oito casos: saída que se sobrepõe à fila, preservação
dos campos não alterados, inicialização repetida, rejeição de leitura
simultânea, rota SI ausente, entrega única, fila cheia e cancelamento ao
encerrar a sessão. O oráculo de boot MIPS usa contratos declarados para as
APIs de controle de alto nível. Separadamente, executa o decodificador
original `__osContGetInitData` sobre pacotes sintéticos de ausência e compara
seus resultados públicos com o backend em três estados iniciais. Ele não
emula PIF, DMA, temporizador nem a implementação privada de SI.

## Evidência e limites

O runner completo passou no Linux x86-64: oito casos de protocolo, três
cenários nativos de TV, três comparações MIPS de boot e três casos do
decodificador original, além do smoke test. As comparações de boot cobriram
17 GPRs, leituras da ROM, escritas AI e RAM sob as máscaras de estruturas
privadas já existentes. O estado do mapa de áudio também foi conferido:
40 slots de som, índice livre 39 e nenhum ponto ativo. Os resultados ficam
em `build/port-controllers/protocol-report.json`,
`build/port-controllers/native-report.json` e
`build/port-controllers/mips-report.json`.

Os três cenários nativos e os oito casos de protocolo também passaram no
Windows x64 da RTX, por console SSH, com Python 3.12.10 usando `-I -O`.
Os hashes do pacote e de cada arquivo foram conferidos; todos os testes
retornaram saída zero. Consulte a [evidência estruturada](validation.json).
A regressão do início de áudio passou, e a compilação N64 manteve a ROM
binariamente idêntica à base US.

Esta etapa não modela controles presentes, entrada física,
PIF/DMA, temporização de boot ou SI privado. A thread de áudio segue sem
processar frames ou amostras; não há som, gráficos nem jogo executável.
Também não há comparação geral de FPR/FCSR.

## Reproduzir

Com a ROM US local e as dependências Python de `port/poc/requirements.txt`:

```sh
build/port-recomp/.venv/bin/python port/controllers/run.py
```

O runner gera `build/port-controllers/proof`, recompila com
`build/port-bootstrap/jfg-toolchain/jfg_recomp_tool`, compila a biblioteca
nativa com `JFG_CONTROLLER_PROFILE=ON` e executa os verificadores. Para o
DLL de diagnóstico Windows x64, após gerar o perfil:

```sh
cmake -S port/poc -B build/port-controllers/windows -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DJFG_BOOT_PROFILE=ON -DJFG_THREAD_PROFILE=ON \
  -DJFG_EVENT_PROFILE=ON -DJFG_INIT_PROFILE=ON -DJFG_AUDIO_MANAGER_PROFILE=ON \
  -DJFG_CONTROLLER_PROFILE=ON \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/port/poc/windows-llvm-mingw.cmake" \
  -DJFG_LLVM_MINGW_ROOT="$PWD/build/port-recomp/cross-toolchain/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64" \
  -DJFG_PROOF_DIR="$PWD/build/port-controllers/proof"
cmake --build build/port-controllers/windows --parallel 2
python3 port/controllers/package_windows.py
```

O pacote inclui os verificadores nativos de boot e protocolo, a DLL e o
perfil. A referência MIPS exige ELF e por isso fica fora do pacote, assim
como ROM, assets e código gerado. O usuário fornece a ROM US local para
executar os verificadores Windows.

A próxima fronteira é `texInitTextures`. A integração dos controles reais
continua pendente; este cenário permite validar o bootstrap sem dispositivo.

Infraestrutura desenvolvida com assistência de OpenAI Codex.
