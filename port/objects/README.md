# Inicialização dos objetos

Este perfil continua `mainInitRlo` a partir da [inicialização de texturas e
modelos](../textures/README.md). Ele executa `objInitObjects` na chamada
`+0x60`, incluindo a inicialização de colisões, iluminação e definições de
explosão. O overlay 33 é carregado durante essa rotina e liberado por ela;
depois do retorno, `mainInitRlo` libera o overlay 34 em `+0x68` e chama
`mainPreNMI` em `+0x70`. A execução para **antes** de
`explosionFlushBlasts`, em `+0x78`. Esta é a primeira entrega do
[plano do port](../ROADMAP.md).

O manifesto seleciona 163 funções originais recompiladas, 26 imports de
plataforma e 49 fronteiras adiadas, num total de 238 entradas em 13 seções.
O código original chama dez auxiliares novos além de `objInitObjects`, entre
eles o helper local de transformação de dados no overlay 34 e
`objInitExplosions` no overlay 33. `Sinf` começa dentro de `Cosf` e aparece
com tamanho zero no ELF; o perfil usa `Arctanf` como limite explícito e
confere os bytes selecionados contra a ROM US.

O verificador lê os índices e dados diretamente da ROM local e confere o
estado criado em RAM. O índice de objetos tem 832 entradas de 16 bits;
`MaxTypes` fica em 814, `Fmax` em 61 e o diretório de explosões contém 21
tipos. A rotina reserva uma sub-região de `0x1B800` bytes, com `0x19000`
bytes de dados e 512 slots, além dos buffers e listas de objetos e colisão.
O teste confere os metadados e índices do subpool, os tamanhos e a separação
das 18 alocações acompanhadas e apenas os buffers que a rotina realmente
zera.

O helper do overlay 34 transforma oito palavras de `Ftables`, delimitadas
por `Findex[5]=13` e `Findex[6]=21`. O verificador reproduz essa transformação
e compara toda a tabela, inclusive os bytes fora desse trecho. Na
inicialização das explosões, os offsets do asset `0x3F` viram ponteiros para
definições no asset `0x40`; também são conferidos os ponteiros internos, os
campos iniciados das definições e os polígonos iniciais. A iluminação padrão
terminou nos três cenários com direção aproximada
`(-0,70710677; 0; 0,70710677)` e metadados coerentes. Nenhuma instância de
objeto é criada por este perfil.

## Evidência e limites

O runner passou no Linux x86-64 com três cenários nativos de TV, oito casos
do protocolo de controles, três comparações com o MIPS original e o smoke
test. Nos cenários NTSC/MPAL, os overlays 33 e 34 foram carregados em
`0x801D8FB0` e `0x801B6D50`; no PAL, em `0x801E07B0` e `0x801BE550`.
Ambos estavam liberados na fronteira final. O oráculo conferiu as quatro
passagens esperadas pelo carregador, 34 transferências da ROM, escritas AI,
16 GPRs restantes e a RAM com as mesmas 22 máscaras de estruturas privadas
do perfil anterior, sem máscaras novas para os dados de objetos. A diferença
conhecida de `a0` é conferida pelos dois valores exatos: `0x1` no MIPS após
a consulta de `resetMsgQueue` e `0xFFFFFFFF800FD800` no import nativo, que
preserva o endereço da fila. Os relatórios locais ficam em
`build/port-objects/native-report.json`,
`build/port-objects/protocol-report.json` e
`build/port-objects/mips-report.json`; a
[evidência estruturada](validation.json) registra os artefatos e seus limites.

O mesmo pacote passou no Windows x64 da RTX: três cenários de inicialização
e oito casos de protocolo, Python 3.12.10 com `-I -O`, execução por console
SSH e saída zero. Os hashes do pacote e de seus arquivos foram conferidos.
A regressão completa do perfil anterior de texturas passou no Linux, e
`make VERSION=us` com comparação binária confirmou a ROM US matching.

Os controles continuam representados por quatro portas ausentes, com a
conclusão SI entregue de forma controlada. Não há leitura de controle físico,
emulação geral de PIF DMA ou prova de temporização. A thread de áudio
permanece esperando mensagens, sem processar frames ou produzir som. A RAM
inicial da iluminação foi conferida, mas isso não prova equivalência geral
de trigonometria, FPR ou FCSR. O perfil não executa
`explosionFlushBlasts`, não renderiza nem inicia uma partida. A comparação
MIPS executa o prefixo da thread de áudio em sequência na mesma imagem de
RAM; ela não prova escalonamento concorrente.

Conforme o plano registrado antes desta entrega, a próxima prioridade é
uma prova gráfica com asset real e a verificação de compatibilidade
F3DJFG/RT64. A integração dos efeitos seguintes do bootstrap aguarda essa
avaliação; não foi incluída por consequência da nova fronteira.

## Reproduzir

Com a ROM US local de SHA-1
`493ced9008dbe932d6e91179b68e8630cf23a023` e as dependências de
`port/poc/requirements.txt`:

```sh
build/port-recomp/.venv/bin/python port/objects/run.py
```

O runner prepara `build/port-objects/proof`, recompila as funções
selecionadas, compila a biblioteca de diagnóstico, executa o smoke test e os
verificadores nativo, de protocolo e MIPS. Para preparar a DLL Windows x64
com o perfil gerado:

```sh
cmake -S port/poc -B build/port-objects/windows -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DJFG_BOOT_PROFILE=ON -DJFG_THREAD_PROFILE=ON \
  -DJFG_EVENT_PROFILE=ON -DJFG_INIT_PROFILE=ON -DJFG_AUDIO_MANAGER_PROFILE=ON \
  -DJFG_CONTROLLER_PROFILE=ON \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/port/poc/windows-llvm-mingw.cmake" \
  -DJFG_LLVM_MINGW_ROOT="$PWD/build/port-recomp/cross-toolchain/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64" \
  -DJFG_PROOF_DIR="$PWD/build/port-objects/proof"
cmake --build build/port-objects/windows --parallel 2
python3 port/objects/package_windows.py
```

O pacote contém a DLL, o manifesto e os verificadores nativos. A comparação
MIPS exige o ELF e fica fora do pacote. ROM, assets extraídos, código gerado
e binários de jogo permanecem em caminhos locais ignorados pelo Git. Antes
de qualquer distribuição pública, é necessário resolver o licenciamento do
código herdado e cumprir os avisos das dependências; consulte
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md). Infraestrutura
desenvolvida com assistência de OpenAI Codex.
