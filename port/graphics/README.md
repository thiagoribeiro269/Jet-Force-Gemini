# Prova gráfica com assets reais: carregamento e comandos

Este pacote é a primeira parte do marco 2 do [roteiro](../ROADMAP.md). Na ROM
US verificada, as rotinas originais de CPU carregaram o modelo 35 (`swdoor`) e
sua textura `0x9097`, descompactaram os dados e produziram duas listas gráficas
na RAM. O modelo contém quatro vértices e dois triângulos; a textura é RGBA16,
32 × 64 pixels. As duas listas representam o mesmo lote de dois triângulos.
Este pacote de carga não executa RSP/RDP ou GPU. A etapa seguinte,
[RT64/D3D12](../rt64/README.md), já produziu uma imagem desse painel e outra
do Juno em pose neutra na RTX; seus resultados e limites ficam separados.

## Evidência de execução

O runner [run.py](run.py) completou o build, o smoke test e a regressão do
protocolo de controles. Os relatórios locais
`build/port-graphics/native-report.json` e
`build/port-graphics/mips-report.json` passaram nos
três cenários de TV (NTSC, MPAL e PAL). Cada carregamento fez cinco
transferências PI e encerrou quatro workers: os três do bootstrap e o worker
adicional que chamou `modLoadModel(35, 0)`. O teste de protocolo passou em
oito casos.

Os três carregamentos e oito casos de protocolo também passaram no Windows
x64 da RTX, por console SSH, com Python 3.12.10 e `-I -O`. Pacote e arquivos
tiveram seus hashes conferidos; todos os testes retornaram saída zero.
Isso valida a execução nativa de CPU no Windows, sem inicializar a GPU.
A [evidência estruturada](validation.json) registra as versões e os limites.

O verificador confere independentemente os bytes descompactados da ROM, a
instância e os caches do modelo e da textura, a geometria, os ponteiros e os
comandos das listas. A lista gerada contém `0x04` (vértices com codificação
DKR), `0x05` (`G_TRIN`, dois triângulos) e `0x07` (`G_DMADL`, seis comandos de
textura). O comando de vértices tem `word1=0`: sua base DMA é fornecida pelo
chamador, portanto a lista isolada não basta para executar a geometria. O
`G_DMADL` aponta para comandos já montados pela rotina de textura.

A comparação MIPS comprova primeiro o bootstrap em execução separada desde a
imagem inicial da ROM, parando em `explosionFlushBlasts`. Depois inicia a
chamada do asset a partir de um snapshot nativo anterior ao carregamento,
tomado após esse bootstrap validado. Para os três cenários, a comparação
confere a sequência de transferências ROM, 14 GPRs de retorno e preservação de
callee-saved, e a RAM inteira exceto quatro regiões específicas do worker
diagnóstico: slot do host, slot do worker, sua pilha e links de espera da fila
DMA. Os dados e alocações do jogo permanecem na comparação. Isso não prova a
equivalência de registradores temporários, FPR/FCSR ou outros modelos.

## Inspeção privada e contrato RT64

[export_private.py](export_private.py) gera PNG, RGBA16, OBJ, MTL, listas e
snapshot de RAM somente sob `build/`, que é ignorado pelo Git. A prévia PNG
decodifica texels; não é um frame do jogo. Para este `LOADBLOCK` com `DXT=0`,
a cópia para TMEM é linear e a amostragem das linhas ímpares troca as duas
palavras de 32 bits de cada grupo de oito bytes (`wordIndex ^ 1`), conforme o
[shader de textura do RT64](https://github.com/rt64/rt64/blob/43373749dac9bbc1b653e6a02aed40a9e1783bed/src/shaders/TextureDecoder.hlsli).
O exportador confere o comando `LOADBLOCK` observado antes de aplicar essa
regra. Os arquivos privados não devem entrar em commits ou pacotes públicos.

O [probe](rt64_probe.py) reproduz somente a identificação GBI do RT64 fixado
em `43373749dac9bbc1b653e6a02aed40a9e1783bed`. Com `--ram` apontando
para o snapshot real de 8 MiB, o relatório local
`build/port-graphics/rt64-probe-report.json`
mostrou `unsupported`: nenhum hash de texto ou dados correspondeu às tabelas.
O [registro GBI](https://github.com/rt64/rt64/blob/43373749dac9bbc1b653e6a02aed40a9e1783bed/src/gbi/rt64_gbi.cpp)
e os [handlers](https://github.com/rt64/rt64/blob/43373749dac9bbc1b653e6a02aed40a9e1783bed/src/gbi/rt64_gbi.h)
fixados não incluem F3DJFG/F3DDKR. O handler F3D genérico de `0x04` não
comprova a codificação DKR, e faltam handlers correspondentes a `0x05` e
`0x07`. O probe não executa RT64 nem testa pixels de GPU.

Para reproduzir localmente com ROM e ELF privados:

```sh
build/port-recomp/.venv/bin/python port/graphics/run.py
```

O probe requer `libxxhash.so.0` no Linux e a cópia exata das fontes do RT64.
Para preparar essa cópia de análise, caso ela ainda não exista:

```sh
git clone https://github.com/rt64/rt64.git build/port-graphics/rt64-source
git -C build/port-graphics/rt64-source checkout 43373749dac9bbc1b653e6a02aed40a9e1783bed
```

Não é necessário compilar RT64 nem inicializar seus submódulos para o probe.
Em seguida, exporte e confira o snapshot real:

```sh
build/port-recomp/.venv/bin/python port/graphics/export_private.py \
  --library build/port-graphics/native/libjfg_poc.so \
  --rom baseroms/baserom.us.z64 \
  --manifest build/port-graphics/proof/manifest.json \
  --out build/port-graphics/preview
build/port-recomp/.venv/bin/python port/graphics/rt64_probe.py \
  --ram build/port-graphics/preview/ram.bin \
  --report build/port-graphics/rt64-probe-report.json
```

Para compilar o diagnóstico Windows após gerar o perfil:

```sh
cmake -S port/poc -B build/port-graphics/windows -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DJFG_BOOT_PROFILE=ON -DJFG_THREAD_PROFILE=ON \
  -DJFG_EVENT_PROFILE=ON -DJFG_INIT_PROFILE=ON -DJFG_AUDIO_MANAGER_PROFILE=ON \
  -DJFG_CONTROLLER_PROFILE=ON \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/port/poc/windows-llvm-mingw.cmake" \
  -DJFG_LLVM_MINGW_ROOT="$PWD/build/port-recomp/cross-toolchain/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64" \
  -DJFG_PROOF_DIR="$PWD/build/port-graphics/proof"
cmake --build build/port-graphics/windows --parallel 2
python3 port/graphics/package_windows.py
```

O próximo passo, M2.2, é escolher e testar uma rota para imagem controlada:
adaptação explícita dos comandos F3DJFG no RT64 ou execução do microcódigo
RSP original seguida da entrega de comandos RDP ao renderizador. A segunda
rota requer mapear o carregamento dinâmico de código do microcódigo, cujo
texto tem `0x1290` bytes e excede a IMEM. O RSPRecomp fixado oferece
`overlay_slots`/`do_overlay_swap`, mas essa alternativa ainda não foi
configurada nem executada para JFG. A base DMA dos vértices, as matrizes e o
estado de desenho do chamador também precisam ser estabelecidos; as listas
capturadas não são uma tarefa de renderização completa. O marco 2 segue
aberto até uma imagem controlada ser observada no renderizador escolhido.

Documentação de escopo anterior: [PLAN.md](PLAN.md). Infraestrutura
desenvolvida com assistência de OpenAI Codex.
