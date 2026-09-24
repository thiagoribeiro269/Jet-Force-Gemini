# Threads cooperativas e chamadas de mensagens do jogo

Este perfil conecta as chamadas de filas e de criação/início de threads ao
código recompilado do JFG. A rotina original `rcpWaitDP` pode suspender enquanto
espera uma mensagem, outra thread executa código do jogo e a primeira retoma
com sua pilha e seus registradores preservados. `mainResetPressed` também usa
a fila real do runtime e conserva o estado de botão pressionado do jogo.

Ainda não executa `boot`, `mainThread` ou `mainInitGame` completos. As mensagens
DP, blur e refração são entradas controladas do teste, sem RSP, RDP ou GPU
produzindo uma imagem. Nenhum resultado desta etapa representa renderização.

## Integração

O perfil contém 63 entradas: **51 funções originais recompiladas**, nove
interfaces de plataforma e três dependências adiadas. Estende a preparação de
memória e módulos já validada, acrescentando `rcpWaitDP`, `mainResetPressed`,
as quatro APIs de filas e `osCreateThread`/`osStartThread`.

O N64ModernRuntime usa threads do computador e semáforos para permitir que
apenas uma thread convidada execute o código do jogo por vez. Quando uma fila
bloqueia, outra thread elegível recebe a execução. O adaptador mantém uma
thread lógica de controle no chamador, com prioridade inferior às threads do
jogo; ela recebe o controle quando não há trabalho pronto e permite que o
teste entregue o próximo evento.

O despachante agora guarda os erros, o destino indireto e os contadores de
chamadas em estado local de cada thread. A RAM e os endereços dos módulos
continuam compartilhados, sob a serialização do agendamento. Isso não permite
escritas concorrentes arbitrárias na RAM convidada.

As rotinas de plataforma recebem os parâmetros da ABI MIPS, incluindo os
argumentos de `osCreateThread` passados pela pilha. A prova inclui uma thread
chamando a criação de outra e uma terceira iniciando essa nova thread. A rotina
original `mainSetMode` é executada pela thread criada.

## Ciclo de vida e adaptação do runtime

O cliente fornece a RAM e áreas reservadas para as estruturas e pilhas. Ele
mantém esses buffers vivos até `jfg_threads_end` terminar. A API aceita uma
sessão por biblioteca, até 32 threads por sessão e um chamador de controle
fixo. O estado 1 indica uma thread iniciada, ainda não concluída; nos pontos
em que o controle volta ao teste, ela pode estar suspensa em uma fila.

`jfg_threads_end` retira as threads pendentes de suas filas, permite que suas
pilhas sejam desenroladas e faz `join` de **todas as threads que criou**. Restaura
a área usada pelo controle e permite abrir outra sessão na mesma biblioteca.
Não inicia o serviço de limpeza do runtime. A fila interna de aposentadoria
da versão fixada é drenada pelo próprio adaptador; se o fechamento falhar,
o cliente deve conservar a RAM e a sessão para tratar o erro.

`kernel.cpp` inclui os fontes fixados de threads e filas na mesma unidade de
compilação para acessar essa fila interna. Não altera arquivos do submódulo,
mas depende dessa revisão específica e precisa ser revisto ao atualizar a
dependência.

Há também uma substituição local de `thread_queue_remove`. A implementação
fixada relê o início da lista ao tentar remover um item posterior. O adaptador
avança pelo elo correto, preserva os demais itens e limpa os vínculos do item
removido. A prova cancela três receptores bloqueados, começando por um que não
está no início da lista. Não foi enviada alteração ao projeto original.

## Verificação

A [evidência estruturada](validation.json) identifica bibliotecas e plataformas.
O diagnóstico nativo contém **12 cenários**, com **21 threads criadas e juntadas**:

- Retorno imediato de `rcpWaitDP` quando não há trabalho ativo.
- Duas ordens de mensagens DP/blur/refração, com execução de `mainSetMode` por
  outra thread enquanto a primeira está suspensa.
- Produtores bloqueados em fila cheia, com envio comum e inserção no início.
- Três receptores atendidos em ordem de prioridade 10, 7 e 5.
- Cancelamento de receptores bloqueados, incluindo remoção fora do início da lista.
- Cancelamento antes do início da thread.
- Consumo e retenção do evento de reset pela rotina original.
- Rejeição de endereço desconhecido e de recriação de uma fila com espera ativa,
  seguida de recuperação do despachante.
- Erro controlado no caminho de tarefa clone, ainda não implementado.
- Criação e início de threads a partir de threads já em execução.

Os 12 cenários passaram no Linux x86-64 e no Windows x64 da RTX, com saída zero
e 21 threads juntadas em cada plataforma. No Windows foi usado Python 3.12.10
com `-I -O`, sem emulador. Os hashes do pacote, da DLL, dos scripts e da ROM
foram conferidos antes da execução.

Uma comparação independente executa `rcpWaitDP` no MIPS original em quatro
configurações de tarefas, usando as rotinas libultra originais com mensagens
previamente disponíveis. Compara o retorno, os registradores preservados pela
ABI e a RAM inteira, excluindo 1.368 bytes de estruturas específicas do runtime,
campos privados das filas e área temporária da pilha. O corpo da rotina é
validado assim; a troca de contexto MIPS e a temporização física não são
comparadas. A suspensão/retomada nativa é coberta pelo diagnóstico separado.

Os 56 pontos diferenciais de memória/carregador passaram novamente com esta
biblioteca. A suíte anterior de 20 funções mantém seus 816 casos aprovados.

## Reproduzir no Linux

Após preparar a ROM US, o ELF matching e as dependências da
[prova inicial](../README.md):

```sh
build/port-recomp/.venv/bin/python port/threads/run.py
```

O comando compila o perfil e executa as verificações de console, memória,
threads e comparação MIPS. Relatórios e logs ficam em `build/port-threads`.
O launcher `checks.py` usa apenas Python padrão; Unicorn e pyelftools são
dependências das verificações de desenvolvimento.

## Windows x64

```sh
cmake -S port/poc -B build/port-threads/windows -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DJFG_BOOT_PROFILE=ON -DJFG_THREAD_PROFILE=ON \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/port/poc/windows-llvm-mingw.cmake" \
  -DJFG_LLVM_MINGW_ROOT="$PWD/build/port-recomp/cross-toolchain/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64" \
  -DJFG_PROOF_DIR="$PWD/build/port-threads/proof"
cmake --build build/port-threads/windows --parallel 2
python3 port/threads/package_windows.py
```

O pacote local inclui DLL, launcher, configuração e hashes, sem ROM ou ELF.
O diagnóstico cria e finaliza apenas threads do próprio processo. Não altera
drivers, serviços ou processos preexistentes.

## Limites e próxima etapa

O [perfil posterior de eventos](../events/README.md) integra relógios, timers
e o scheduler original em execução sem renderização. Faltam as demais APIs
de threads, vídeo e outras dependências de `mainInitGame`. Este perfil não oferece
preempção de hardware nem trata todos os formatos de alteração de código.
`amSndStop`, `amAmbientStop` e `TrapDanglingJump` permanecem explícitos como
dependências não executáveis. O caminho clone não devolve sucesso fictício.

A sequência US inicia `osCreateScheduler` antes de preparar o heap; a ordem
da variante kiosk não deve ser usada como referência para o próximo passo.

Infraestrutura desenvolvida com assistência de OpenAI Codex. A base do jogo
e os arquivos dos submódulos permanecem preservados.
