# Inicialização e primeira espera da thread de áudio

O port agora completa o `amInit` original, executa `amGo` e inicia `__amMain`.
A thread registra seu cliente no scheduler e bloqueia aguardando uma mensagem.
O programa principal libera o overlay 25 de inicialização e continua em
`mainInitRlo`, parando antes de `amInitAudioMap` em `0x80002830`, chamada pelo
overlay 36 em `+0x2C`. **Ainda não há síntese de amostras, saída de som, imagem
ou boot completo.**

O perfil tem 223 entradas: 145 funções originais recompiladas, 24 interfaces
de plataforma e 54 caminhos explicitamente adiados. Usa o runtime C/C++ das
etapas anteriores; os fontes do jogo e os vendors permanecem preservados.

## Estado alcançado

`amInit` termina as configurações de volume, vibrato e surround, libera sua
tabela temporária, cria a fila de controle de animação e a associa ao player
de música. O player de ambiente recebe mais um evento de volume, ficando com
dois eventos na fila; o player de música conserva seu evento de banco. Os
callbacks que processam essas filas continuam adiados.

O cliente de áudio, ID 1, fica na cabeça da lista do scheduler, preservando o
cliente anterior de ID 2. Seu primeiro `osRecvMesg`, chamado em `0x80001DFC`,
aguarda a fila `D_800F1AF4_B9AA4`. O teste exige tanto o estado ativo da thread
quanto sua presença na lista de espera dessa fila vazia. Também confere os
slots de mensagem, o retorno salvo na pilha e os dados públicos do cliente.

Na fronteira principal, a base do overlay 25 está zerada e `amInit` já não
está registrada como chamável. O overlay 36 continua carregado. A fila de
animação tem capacidade um; o player de música aponta para ela. O estado de
saída surround observado é `[1,0,0]`. O heap de áudio conserva 192.528 bytes
usados e as tabelas/bancos das etapas anteriores continuam conferidos.

## Despertar e dependências ainda ausentes

Dois cenários adicionais exercitam a thread nativa após a inicialização:

- Uma mensagem de tipo 4 é recebida e percorre o ramo original que volta à
  espera. O ponteiro da mensagem aparece no slot da pilha; a fila volta a
  ficar vazia com a thread bloqueada.
- Uma mensagem de tipo 1 solicita processamento de áudio. A execução falha
  explicitamente em `__amHandleFrameMsg`, na chamada `0x80001E34`, com status
  `-3`. O corpo ainda não foi integrado e não devolve sucesso fictício.

Nenhum cenário abre um dispositivo de som ou processa amostras. A configuração
AI permanece igual durante esses testes. O encerramento cancela e junta os
três workers da sessão, incluindo a thread bloqueada ou já encerrada por erro.

## Referência MIPS e limites da comparação

O oráculo parte da imagem original antes do bootstrap nativo. Primeiro executa
`mainInitGame` até a mesma fronteira; depois executa o prefixo original de
`__amMain` na mesma RAM MIPS. Intercepta a primeira chamada bloqueante à fila
vazia antes de entrar no kernel de recebimento. Essa execução serial compara
os efeitos de CPU e memória, não o escalonamento ou a temporização do console.

A RAM resultante e os rastros de ROM/AI são iguais nos três parâmetros de TV.
As exclusões são as estruturas privadas de threads, campos das listas de
espera e as pilhas anteriores já documentadas. **O novo frame da thread de
áudio e os clientes do scheduler estão incluídos na comparação.**

Dezesseis GPRs da thread principal são comparados diretamente. Há uma diferença
conhecida e verificada separadamente em `a0`: a última consulta não bloqueante
à fila de reset deixa `1` no MIPS libultra, por causa de `__osRestoreInt`, enquanto
o import nativo preserva o endereço de `resetMsgQueue`. A entrada seguinte,
`amInitAudioMap`, não recebe argumentos e sobrescreve `a0` antes de usá-lo.
O teste exige esses dois valores exatos e registra ambos; não ampliou as
máscaras de RAM para contornar a diferença. Não há prova geral de FPR/FCSR.

## Resultados e reprodução

As [evidências estruturadas](validation.json) registram hashes e limites.
Passaram três cenários de inicialização e dois de despertar no Linux x86-64
e no Windows x64 da RTX, com 15 workers encerrados em cada suíte nativa.
As três comparações MIPS passaram. A regressão dos players anteriores passou
novamente, e `make VERSION=us` com comparação binária confirmou a ROM matching.

```sh
build/port-recomp/.venv/bin/python port/audio_start/run.py
```

Para Windows, após gerar o perfil:

```sh
cmake -S port/poc -B build/port-audio-start/windows -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DJFG_BOOT_PROFILE=ON -DJFG_THREAD_PROFILE=ON \
  -DJFG_EVENT_PROFILE=ON -DJFG_INIT_PROFILE=ON -DJFG_AUDIO_MANAGER_PROFILE=ON \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/port/poc/windows-llvm-mingw.cmake" \
  -DJFG_LLVM_MINGW_ROOT="$PWD/build/port-recomp/cross-toolchain/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64" \
  -DJFG_PROOF_DIR="$PWD/build/port-audio-start/proof"
cmake --build build/port-audio-start/windows --parallel 2
python3 port/audio_start/package_windows.py
```

O pacote foi executado por SSH de console no Windows, Python 3.12.10 com
`-I -O`, hashes verificados e saída zero. Processos e serviços preexistentes
foram preservados. A ROM é fornecida localmente; ROM, assets, ELF, C gerado e
binários permanecem fora do Git.

O pacote inclui o [inventário de dependências](../THIRD_PARTY_NOTICES.md) e
os textos GPLv3 do runtime e MIT do N64Recomp. Isso não representa auditoria
completa de uma distribuição pública nem atribui licença nova ao jogo herdado.

O próximo passo é integrar `amInitAudioMap` e continuar o bootstrap; em
paralelo técnico, o caminho de frames precisa de síntese, tarefas RSP e saída
de amostras. Esses trabalhos ainda não estão concluídos.

Infraestrutura desenvolvida com assistência de OpenAI Codex. Subagentes
GPT-6-Sol com esforço médio prepararam o perfil, o oráculo e o inventário;
o coordenador integrou, revisou e validou os resultados.
