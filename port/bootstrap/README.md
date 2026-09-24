# Bootstrap com o carregador dinâmico original

O port agora executa `TrapDanglingJump`, carrega o overlay 36 e entra em
`mainInitRlo`. Essa rotina chama `mainPreNMI` e solicita `amInit`; o carregador
original então carrega o overlay 25. A execução para explicitamente na entrada
de `amInit`, antes de executar sua inicialização de áudio. **O boot completo,
a imagem e a saída de áudio ainda não estão disponíveis.**

Este perfil preserva o [diagnóstico anterior](../init/README.md), que continua
parando antes do overlay 36. A base do jogo e os submódulos não foram alterados.

## Implementação

O perfil possui 175 entradas: 103 funções originais recompiladas, 23 interfaces
de plataforma e 49 funções explicitamente adiadas. Estar recompilada não
significa que todos os caminhos de uma função já foram exercitados.

A descoberta de dependências consulta as tabelas reais de relocação dos
overlays. Chamadas externas com endereço zero na ROM são resolvidas pela
tabela de símbolos; chamadas locais do tipo 2 usam o imediato da instrução.
As relocações internas dos corpos adiados não entram na recompilação.

A opção `live_relocated_calls` faz as chamadas `R_MIPS_26` selecionadas
consultarem a instrução JAL que o próprio `runLink` escreveu na RAM. Assim,
uma dependência ainda ausente passa por `TrapDanglingJump`, que carrega seu
módulo e corrige as chamadas. Uma segunda chamada já corrigida chega à mesma
fronteira de áudio sem novas leituras da ROM. Os perfis anteriores conservam
seu modo de despacho. Isso não implementa suporte geral a código automodificado.

O acompanhamento da tabela de módulos pode ser armado antes de `mainInitGame`.
Somente o par de globals inicialmente zerado é aceito como pendente. Depois
da primeira tabela válida, as verificações são estritas, inclusive antes de
executar uma nova chamada, para rejeitar endereços obsoletos sem efeitos no jogo.

O salto final `JR t8` de `TrapDanglingJump` usa o destino capturado antes do
delay slot e preserva o retorno ao chamador. Quando uma dependência está
ausente, o despachante guarda destino, callsite e os 32 GPRs antes do `longjmp`.
Esse snapshot só é garantido para falhas de procura de função.

## Validação observada

Os [resultados estruturados](validation.json) registram versões e hashes.

- Três cenários de bootstrap, nos parâmetros de TV 1, 0 e 2, passaram no
  Linux x86-64 e no Windows x64 da RTX. Cada cenário realizou oito leituras
  da ROM e encerrou suas duas threads.
- Nove verificações do linker passaram nos dois sistemas: estado inicial,
  rejeição de globals/tabelas inválidos, tabela real, JAL já corrigido sem nova
  leitura, JAL corrompido, rejeição sem efeitos após invalidar a tabela e unbind.
- A referência MIPS executou os dois trampolins e parou antes de `amInit`, nos
  três parâmetros de TV. Os rastros de leitura da ROM e a RAM foram iguais,
  com as exclusões explicitadas no relatório. Dezessete GPRs foram comparados:
  zero, argumentos, registradores preservados, global pointer, stack e retorno.
- As regressões de inicialização e descompressão anteriores e os 816 casos
  diferenciais de CPU passaram. Três testes da descoberta de dependências
  passaram. `make VERSION=us` e a comparação binária confirmaram a ROM matching.

Nos parâmetros 1 e 2, o módulo 36 foi alocado em `0x801852E0` e o módulo 25 em
`0x80185F80`. A fronteira foi `0x80186288`, chamada em `0x801852F4`. Com parâmetro
0, essas bases foram `0x8018CAE0` e `0x8018D780`. Os testes calculam os endereços
a partir da tabela real, sem fixar o resultado do alocador.

A comparação de RAM exclui estruturas privadas de threads, ponteiros de espera
das filas e pilhas de chamadas. O oráculo entrega DMA imediatamente; o port
usa seu controlador cooperativo. Não há prova de temporização do console ou
comparação geral dos registradores da FPU. O corpo de áudio não é executado
nem substituído por um retorno fictício de sucesso.

## Reproduzir

Com a ROM e o ambiente descritos na [documentação principal](../README.md):

```sh
build/port-recomp/.venv/bin/python port/bootstrap/run.py
build/port-recomp/.venv/bin/python -m unittest discover -s port/boot -p test_closure.py
```

Para Windows, após gerar o perfil:

```sh
cmake -S port/poc -B build/port-bootstrap/windows -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DJFG_BOOT_PROFILE=ON -DJFG_THREAD_PROFILE=ON \
  -DJFG_EVENT_PROFILE=ON -DJFG_INIT_PROFILE=ON \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/port/poc/windows-llvm-mingw.cmake" \
  -DJFG_LLVM_MINGW_ROOT="$PWD/build/port-recomp/cross-toolchain/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64" \
  -DJFG_PROOF_DIR="$PWD/build/port-bootstrap/proof"
cmake --build build/port-bootstrap/windows --parallel 2
python3 port/bootstrap/package_windows.py
```

O pacote inclui DLL e diagnósticos Python, usa a ROM local do usuário e não
inclui ROM, ELF, fontes gerados ou assets. Foi executado por SSH de console
no Windows com Python 3.12.10, `-I -O`, hashes conferidos e saída zero. Somente
as threads dos próprios diagnósticos foram encerradas.

A próxima dependência é `amInit` e sua cadeia de inicialização do áudio.
Renderização F3DJFG, controles, saves e gameplay continuam pendentes.

Infraestrutura desenvolvida com assistência de OpenAI Codex, incluindo três
subagentes GPT-6-Sol com esforço médio e revisão/integração pelo coordenador.
