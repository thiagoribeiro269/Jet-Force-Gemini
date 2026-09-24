# Scheduler original, eventos e temporizadores

O perfil executa `osCreateScheduler` e a thread original `__scMain` antes de
preparar heap e `runLink`, seguindo essa parte da ordem US. O scheduler recebe
pulsos de vídeo, atualiza seus contadores e envia mensagens aos clientes
registrados. O caminho PRENMI também alcança `mainResetPressed` pelo código
original. O evento pertence ao jogo; não solicita desligamento do computador.

**Ainda não há renderização.** O serviço de VI mantém configuração, estado de
tela preta e notificações, sem produzir imagens. As rotinas que executariam
trabalho gráfico ou de áudio continuam explicitamente indisponíveis. Um
evento de RSP entregue a esse caminho causa erro controlado, sem fingir que a
tarefa gráfica foi concluída.

## Código incluído

São 103 entradas: 61 funções originais recompiladas, 19 interfaces de
plataforma e 23 caminhos adiados. A contagem descreve o conjunto incluído,
não uma cobertura de todos os caminhos do jogo.

Além das funções das etapas anteriores, este perfil inclui o scheduler,
seu processamento de retrace e as rotinas de adicionar/remover clientes.
As interfaces novas cobrem eventos, relógio, timer, configuração básica de
VI e a máscara lógica de interrupções.

Cinco funções locais do ELF foram emitidas com tamanho zero pelo compilador
original. `prepare_events.py` declara seus símbolos finais explicitamente.
A preparação exige que o final seja a próxima função na mesma seção, rejeita
cruzamento de outros símbolos de função e continua comparando os bytes com a
ROM verificada. O ELF e os fontes herdados são preservados.

## Relógio e entrega das mensagens

O laço pertence à sessão e é processado pelo controlador quando as threads do
jogo devolvem o controle. Há dois modos:

- Relógio monotônico do computador, com espera até o próximo prazo ou limite
  indicado pelo chamador.
- Relógio controlado, avançado explicitamente pelos testes para conferir
  fronteiras, overflow e ordem dos eventos.

O contador usa a unidade de 46.875.000 ticks por segundo. `osGetCount` retorna
os 32 bits baixos; `osGetTime` e `osSetTime` usam a ABI de 64 bits. O tempo
ajustável usa um deslocamento sobre o contador monotônico, conforme o contrato
do runtime. Ajustar esse tempo não altera os prazos dos timers.

`osSetTimer` recebe os argumentos de 64 bits nos registradores e na pilha
MIPS. Timers únicos e periódicos enviam mensagens sem bloqueio. Fila cheia
descarta a notificação e preserva as mensagens existentes. Em prazos iguais,
a inserção mais recente é atendida primeiro, como na rotina original de
inserção testada. Períodos perdidos são agrupados; o próximo período começa
no instante de atendimento, sem gerar uma fila ilimitada de atrasos.

O timer ativo precisa ser cancelado antes de reutilizar o mesmo endereço.
`jfg_events_cancel_timer` é uma API do adaptador: esta ROM não possui um
símbolo `osStopTimer` que pudesse ser registrado como chamada de jogo.
O encerramento da sessão cancela todos os timers antes de cancelar e juntar
as threads convidadas. Nenhuma thread de timer fica em segundo plano.

`osSetEventMesg` mantém as 15 entradas da tabela convidada. O teste pode
entregar um evento registrado com `jfg_events_post`. A configuração de VI
cobre os modos usados pelo scheduler: NTSC/MPAL a 60 Hz nominais e PAL a 50 Hz
nominais, incluindo o divisor de retraces de `osViSetEvent`.

O contrato é cooperativo: a máscara de interrupções é registrada, mas não há
preempção física de CPU. O relógio, a chegada das mensagens e a representação
privada dos timers não constituem uma simulação ciclo a ciclo do console.
Há limites de 256 timers ativos e 4.096 atendimentos por passagem; o excesso
retorna erro de serviço, sem descartar silenciosamente trabalho pendente.

## Verificação

A [evidência estruturada](validation.json) identifica plataformas e artefatos.
O diagnóstico nativo tem **15 cenários**, com **nove threads criadas e juntadas**:

- Scheduler original nos três modos de vídeo, notificações aos clientes e
  remoção de cliente.
- Divisor de retraces e evento PRENMI chegando à rotina de reset do jogo.
- Registro de todos os eventos, desativação e rejeição de ID inválido.
- Timer único acordando uma thread, timer periódico, cancelamento e reutilização.
- Fila cheia, prazos iguais e argumentos de 64 bits.
- Contador de 32 bits atravessando seu limite, ajuste de tempo e overflow de
  prazos de 64 bits.
- Encerramento com timer pendente e uma thread esperando sua mensagem.
- Timer com relógio monotônico real e erro explícito no caminho RSP indisponível.

Os 15 cenários passaram no Linux x86-64 e no Windows x64 da RTX, com saída zero
e nove threads juntadas em cada execução. Os 12 cenários anteriores de threads
também passaram nessa biblioteca nas duas plataformas, com 21 joins em cada
rodada. O teste Windows usou Python 3.12.10 com `-I -O`, sem emulador, após
conferir os hashes do pacote, arquivos e ROM.

A referência independente executa as rotinas MIPS originais, com controle do
contador, do registrador de comparação e das operações de interrupção.
Passaram **43 comparações** em dez grupos: tabela de eventos, retornos de
tempo, mensagens de timers e estado observável de retraces sem tarefas
gráficas. As amostras da ABI de tempo começam com o contador base zerado;
elas não comprovam igualdade de todas as fases do clock da libultra física.

Os testes não comparam os campos privados das listas de timers com o mapa de
prazos do computador. Para o retrace, comparam o contador de frames, o contador
global de retraces e a fila/buffer do cliente. Não alegam equivalência de toda
a RAM ou dos registradores nesta nova referência.

As regressões da mesma biblioteca mantêm os 56 pontos de heap/carregador,
12 cenários de threads e quatro comparações MIPS de `rcpWaitDP`. A prova básica
de CPU mantém seus 816 casos aprovados.

## Reproduzir

Com a ROM US local, ELF matching e dependências da [prova de CPU](../README.md):

```sh
build/port-recomp/.venv/bin/python port/events/run.py
```

O comando gera o perfil, compila a biblioteca e executa verificações e
regressões. Logs e relatórios ficam em `build/port-events`.

Para preparar Windows x64 depois da geração do perfil:

```sh
cmake -S port/poc -B build/port-events/windows -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DJFG_BOOT_PROFILE=ON -DJFG_THREAD_PROFILE=ON -DJFG_EVENT_PROFILE=ON \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/port/poc/windows-llvm-mingw.cmake" \
  -DJFG_LLVM_MINGW_ROOT="$PWD/build/port-recomp/cross-toolchain/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64" \
  -DJFG_PROOF_DIR="$PWD/build/port-events/proof"
cmake --build build/port-events/windows --parallel 2
python3 port/events/package_windows.py
```

O pacote local contém DLL, launcher, configuração e hashes, sem ROM, ELF ou
caminhos privados. O launcher exige Python x64 3.11+ e usa apenas a biblioteca
padrão. Não altera drivers, serviços ou outros processos.

## Próxima etapa

Integrar as demais dependências de `mainInitGame`, incluindo inicialização de
vídeo, periféricos/DMA e RCP, mantendo erros explícitos onde falta suporte.
Ainda faltam boot completo, renderização F3DJFG, áudio, controles, saves e uma
partida jogável. Contadores avançando e mensagens de VI não são frames desenhados.

Infraestrutura desenvolvida com assistência de OpenAI Codex. Os fontes do jogo
e dos submódulos permanecem na base escolhida.
