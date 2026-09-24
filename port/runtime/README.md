# Filas de mensagens do runtime

Este adaptador liga uma API C ao núcleo de filas do N64ModernRuntime fixado no
projeto. Compila os fontes originais de `mesgqueue`, `threadqueue`, `scheduling`
e `threads` sem alterá-los. A interface está validada separadamente e ainda não
é chamada pela inicialização recompilada do jogo.

São suportados criação da fila, envio no final (`osSendMesg`), inserção no início
(`osJamMesg`) e recebimento (`osRecvMesg`). A ordem, os índices do buffer circular,
os retornos de fila cheia/vazia e as mensagens de 32 bits são mantidos.

O adaptador serializa as chamadas e **não inicia threads de jogo**. Aceita o
sinalizador de bloqueio somente quando a operação pode terminar imediatamente.
Se for necessário suspender ou acordar uma thread, retorna erro explícito sem
alterar a memória. O agendamento e a integração com as chamadas do jogo são os
próximos passos.

## Interface

`jfg_queue_call(operation, rdram, ram_size, queue, value, argument, guest_result)`
recebe RAM de 8 MiB com a mesma representação em palavras usada na prova de CPU.
As operações são 0=criar, 1=enviar, 2=inserir no início e 3=receber. Ao criar,
`value` é o endereço do buffer e `argument` a capacidade; nas demais operações,
`argument` é `OS_MESG_NOBLOCK` ou `OS_MESG_BLOCK`.

O status do adaptador é separado do retorno da API do N64:

| Status | Significado |
| --- | --- |
| `0` | Operação executada; `guest_result` contém o retorno da API (`0` para a criação, cujo retorno original é vazio). |
| `-1` | Endereço, limite, sobreposição ou estado de fila inválido. |
| `-2` | Operação ou sinalizador não suportado. |
| `-3` | Seria necessário suspender ou acordar uma thread ainda não integrada. |
| `-4` | Exceção da implementação nativa. |

Por exemplo, enviar sem bloqueio para uma fila cheia retorna status `0` e
`guest_result=-1`, como na API original. Uma operação bloqueante nessa fila
retorna status `-3`, mantendo o valor de saída fornecido pelo chamador.

## Evidência e limites

A [evidência desta etapa](validation.json) registra **591 verificações** nas
capacidades 1, 2, 4 e 16. Há 463 operações comparadas com a execução MIPS
original e 128 rejeições do adaptador verificadas sem alteração de RAM ou saída.
Das 463, quatro criam filas e 459 têm retorno público comparado.

Os mesmos 591 casos passaram na biblioteca nativa do Linux e no Windows x64 da
RTX, com saída zero. O teste Windows usou Python 3.12.10, sem emulador e com os
hashes do pacote, das expectativas e da DLL conferidos. Foram rejeitadas 104
operações que exigiriam suspensão e 24 entradas inválidas.

No Linux, o oráculo executa as quatro rotinas libultra originais da ROM US no
Unicorn. Apenas as operações de interrupção usam um contrato serializado,
sem eventos assíncronos. A comparação cobre o retorno da API e os 8 MiB de RAM,
com duas exclusões explícitas: oito bytes de campos privados de espera da fila
e 576 bytes de área temporária da pilha MIPS. O runtime usa ponteiros nulos
onde libultra usa uma thread sentinela; chamadas do computador também não
escrevem os mesmos frames da pilha convidada.

Os testes geram expectativas e hashes de memória, sem copiar conteúdo da ROM.
`queue_checks.py` reaplica os casos usando apenas Python padrão e a biblioteca
nativa. Essa execução não usa emulador, não compara registradores internos do
runtime e não comprova temporização ou escalonamento do console.

Não são iniciados renderizador, áudio, timers, serviços de limpeza ou threads
de jogo. Os testes não encerram processos ou alteram serviços preexistentes.

## Reproduzir no Linux

É necessário ter o ELF N64 matching, a ROM US e o ambiente de testes da
[prova de CPU](../README.md). A nova biblioteca exige compilador C++20.

```sh
cmake -S port/runtime -B build/port-runtime/native -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/port-runtime/native --parallel 2
build/port-recomp/.venv/bin/python port/runtime/verify_queues.py \
  --library build/port-runtime/native/libjfg_queues.so
python3 -I port/runtime/queue_checks.py \
  --library build/port-runtime/native/libjfg_queues.so \
  --rom baseroms/baserom.us.z64 \
  --fixtures build/port-runtime/queue-fixtures.json \
  --report build/port-runtime/linux-report.json
```

## Preparar Windows x64

```sh
cmake -S port/runtime -B build/port-runtime/windows -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/port/poc/windows-llvm-mingw.cmake" \
  -DJFG_LLVM_MINGW_ROOT="$PWD/build/port-recomp/cross-toolchain/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64"
cmake --build build/port-runtime/windows --parallel 2
python3 port/runtime/package_windows.py
```

O CMake gera um cabeçalho de compatibilidade para a diferença entre `Windows.h`
e `windows.h` durante a compilação cruzada no Linux. Os fontes do fornecedor
permanecem intactos. O pacote fica em `build/port-runtime`, sem ROM ou ELF,
e contém instruções, DLL, launcher, expectativas geradas e hashes.

Desenvolvimento deste adaptador assistido por OpenAI Codex. As expectativas
funcionais vêm da execução independente do código MIPS original.
