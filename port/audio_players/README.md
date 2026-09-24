# Players de sequências e efeitos

O port agora executa as duas chamadas de `n_alCSPNew` e a criação de efeitos
por `gsSndpNew`. O percurso original de `amInit` para explicitamente antes de
`amGo`, em `0x80001EA0`, chamado pelo overlay 25 em `+0x6B4`. **A thread de
áudio continua criada e parada; ainda não há processamento de amostras ou som.**

O perfil contém 220 entradas: 136 funções originais recompiladas, 24 interfaces
de plataforma e 60 funções explicitamente adiadas. Reutiliza o runtime da
[etapa do gerenciador](../audio_manager/README.md), sem mudanças nos fontes
do jogo ou nos vendors.

## Estado original conferido

O sintetizador mantém os três clientes na ordem efeitos, ambiente e música,
porque cada construtor insere seu cliente no início da lista. Foram conferidos
os ponteiros de callback, o `clientData`, os tempos iniciais e as listas livres.
Os callbacks dos players ficam registrados como dependências adiadas; seus
endereços são gravados pelo jogo, mas seus corpos não executam nesta etapa.

| Player | Estruturas livres de voz/estado | Eventos livres | Eventos na fila |
| --- | ---: | ---: | ---: |
| Sequências de música | 32 | 149 de 150 | 1 |
| Sequências de ambiente | 16 | 49 de 50 | 1 |
| Efeitos | 32 | 200 de 200 | 0 |

Cada player de sequências tem 16 canais, volume inicial `0x7FFF` e
`nextEvent.type=9`. Depois de chamar o construtor, o auxiliar original do
overlay 25 posta um evento `0xE`, com delta zero e o ponteiro do banco no
payload. Esse evento continua aguardando processamento na fronteira atual.
Por isso as filas não estão vazias ao chegar a `amGo`, embora estivessem vazias
imediatamente após `n_alCSPNew`.

O construtor de efeitos posta e retira seu evento inicial `0x20`, deixando
`nextDelta=16.000` microssegundos, fila ativa vazia e todos os eventos livres.
Os cinco grupos de volume começam em `0x7FFF`.

O verificador percorre as listas com limites, detecta ciclos, confere os
ponteiros anteriores, impede que um nó esteja simultaneamente nas listas
livre e ativa e confirma que ambas pertencem ao mesmo conjunto de alocações.
O heap de áudio usa 192.528 de 194.960 bytes nesta fronteira.

O cenário representa uma inicialização com BSS zerada. `alHeapDBAlloc` não
limpa cada alocação; alguns campos e o início da lista livre de efeitos
dependem desse estado inicial. O teste não afirma que esses construtores
podem ser reutilizados sobre memória arbitrariamente suja.

## Evidências

Os [resultados estruturados](validation.json) registram versões e hashes.
Os três parâmetros de TV da ROM US passaram no Linux x86-64 e no Windows x64
da RTX. Em cada cenário, o diagnóstico conferiu os três players e encerrou
suas três threads. `amGo` permaneceu como falha controlada, com destino e
callsite verificados antes de encerrar a sessão.

A referência MIPS executou o mesmo percurso até a entrada de `amGo` e
comparou 17 GPRs, rastros de leitura da ROM, escritas da interface AI e RAM.
As exclusões continuam limitadas a estruturas privadas de threads, campos
de espera das filas e pilhas identificadas no relatório. A memória dos
players, seus eventos e o heap de áudio estão incluídos na comparação.

A regressão do gerenciador anterior passou nos três cenários nativos, nas
três comparações de inicialização MIPS e nos 30 casos de frequência. A ROM
reconstruída continua binariamente idêntica à base US. Não há validação
geral da FPU, de temporização física ou de reprodução de amostras.

## Reproduzir

```sh
build/port-recomp/.venv/bin/python port/audio_players/run.py
```

Para Windows, após a geração:

```sh
cmake -S port/poc -B build/port-audio-players/windows -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DJFG_BOOT_PROFILE=ON -DJFG_THREAD_PROFILE=ON \
  -DJFG_EVENT_PROFILE=ON -DJFG_INIT_PROFILE=ON -DJFG_AUDIO_MANAGER_PROFILE=ON \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/port/poc/windows-llvm-mingw.cmake" \
  -DJFG_LLVM_MINGW_ROOT="$PWD/build/port-recomp/cross-toolchain/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64" \
  -DJFG_PROOF_DIR="$PWD/build/port-audio-players/proof"
cmake --build build/port-audio-players/windows --parallel 2
python3 port/audio_players/package_windows.py
```

O pacote foi executado por SSH de console no Windows com Python 3.12.10,
`-I -O`, hashes verificados e saída zero. A ROM é fornecida localmente.
ROM, assets, ELF, código gerado e binários ficam fora do Git. Processos e
serviços preexistentes foram preservados.

O próximo passo é integrar `amGo` e a execução controlada de `__amMain`,
seguindo a parte restante de `amInit`. Eventos dos players, processamento
de amostras/RSP, saída de som, renderização e boot completo continuam pendentes.

Infraestrutura desenvolvida com assistência de OpenAI Codex. Dois subagentes
GPT-6-Sol com esforço médio prepararam o perfil e as verificações dos players;
o coordenador integrou e validou os resultados.
