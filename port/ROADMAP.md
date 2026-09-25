# Plano do port Windows x64

**Revisão de arquitetura em 25/09/2026:** Thiago escolheu eliminar RT64 e
toda emulação, inclusive a proposta CIC. O caminho ativo passa a ser
[port nativo com Direct3D e conversão de assets](native/PLAN.md). A prova
RT64 descrita abaixo é histórica, preservada no commit `b1753bb`; ela não
é mais uma dependência nem comprovação da nova camada gráfica. As referências
MIPS deixaram de ser executadas. O restante do roteiro exige replanejamento
à medida que as adaptações nativas substituírem os contratos de hardware.

**Avanço da rota nativa:** renderização D3D11 própria do personagem composto
e primeiro clipe original de animação executado em C++ concluídos no escopo
de diagnóstico. O clipe 1026 produziu 32 amostras distintas, retorno exato
ao primeiro frame e regressão da pose neutra aprovada. Ainda falta ligar
seleção/transição de animações ao estado do jogo e integrar o ciclo nativo.
Detalhes em [native/README.md](native/README.md).

O controlador nativo já seleciona e mistura os clipes 1026/1030, com tempo
em segundos, interrupção de transições e preservação do relógio em comandos
repetidos. O novo controlador do personagem liga esses clipes e a pose
constante 1071 a comandos nativos de teste: repouso, deslocamento e postura
baixa, com posição X/Z e giro. A sequência de 180 frames passou na RTX;
a política é provisória, sem colisão e sem integração à lógica original.
O próximo bloco deve mapear os estados/remapeamentos do controle original
do Juno para definir essa ligação. [Evidência](native/character-validation.json).

Planejamento registrado antes da implementação do próximo bloco, a partir
do checkpoint `68cfa4d`. O objetivo é chegar a uma partida de JFG no Windows
x64 com NVIDIA. A base do jogo permanece em
`f409c111053c671ae91051a6cdff277e0af7d95e`.

## Ponto comprovado

O [perfil de texturas](textures/README.md) executa o bootstrap até a entrada
de `objInitObjects`, com o overlay 34 carregado. Memória, filas, threads,
carregador, dados de áudio e diretórios de texturas/modelos têm evidências
nativas e MIPS delimitadas. A [prova de assets reais](graphics/README.md)
agora carrega e descompacta o modelo 35 e sua textura `0x9097`, com listas
geradas na RAM. A [prova RT64](rt64/README.md) produziu na RTX, pelo Windows
D3D12, uma imagem desse painel e outra do Juno em pose neutra com 21 ossos.
O renderer ainda não está ligado ao ciclo do jogo; não há saída de som ou partida.

O número de funções selecionadas descreve cada diagnóstico; ele não mede
percentual de conclusão do jogo. Os marcos abaixo exigem comportamento
observável e evidência reproduzível.

## Entrega atual: inicialização dos objetos

**Estado: concluída e validada no Linux e no Windows x64.** O escopo foi
registrado no commit `1a478ed`, antes da implementação; os
[resultados e limites](objects/README.md) estão documentados separadamente.
O pacote começa em `objInitObjects`, chamado
por `mainInitRlo+0x60`. Deve completar seu retorno, executar a liberação do
overlay 34 em `+0x68` e `mainPreNMI` em `+0x70`, e parar antes de
`explosionFlushBlasts` em `+0x78`. Essa fronteira foi escolhida na análise
estática, antes de executar o novo perfil.

A análise das realocações e da árvore de chamadas identificou dez auxiliares
novos além de `objInitObjects`:

| Grupo | Funções | O que precisa ser conferido |
| --- | --- | --- |
| Memória e estado | `mmAllocRegion`, `hitInit`, `resetVars` | Sub-região do heap, buffers, listas e contadores iniciais |
| Iluminação | `lightDefaultObjectLight`, `lightSetObjectLight`, `mathOneFloatRPY`, `Cosf`, `Sinf` | Dados finais da iluminação e operações de ponto flutuante do caminho real |
| Dados compactados | `func_overlay_34_022002C8_1F54D90` | Transformação do trecho selecionado, preservando os bytes externos |
| Explosões | `objInitExplosions` | Inicialização no overlay 33, tabelas de assets e liberação do módulo |

`mmAlloc`, `piRomLoad`, `piRomGetFileSize` e `runlinkFreeCode` já estão
cobertos pela infraestrutura existente. A árvore examinada não introduz
chamada direta a renderizador ou novo dispositivo. Chamadas indiretas e
limites de dados devem ser conferidos antes de declarar o pacote completo.

Há uma particularidade já identificada: `Sinf` é uma entrada interna de
`Cosf`, com tamanho zero no ELF. A seleção deve usar o limite explícito
`Arctanf`, conferir seus bytes contra a ROM e preservar ambas as entradas.
Uma igualdade de RAM para a iluminação inicial não constitui uma prova
geral de FPU, FCSR ou de todos os ângulos possíveis.

O inventário inicial de dados confirmou as seções `0x30` (índice de objetos),
`0x2E` (diretório de definições), `0x18`/`0x19` (`Ftables`/`Findex`) e `0x3F`
(diretório usado por explosões), acompanhado pelos dados da seção `0x40`.
Os valores esperados da ROM US são
`objindex_max=831`, `MaxTypes=814`, `Fmax=61` e 21 tipos de explosão. O
helper transforma oito palavras de `Ftables`, delimitadas por
`Findex[5]=13` e `Findex[6]=21`. A sub-região solicita `0x19000` bytes e
512 slots; a reserva externa inclui os metadados do alocador. Esses valores
serão conferidos nos dados reais antes de usar ponteiros ou contar entradas.

### Sequência de execução

1. Concluir o inventário de globais, seções da ROM, contadores e limites das
   alocações. Registrar entradas e saídas esperadas dos auxiliares.
2. Preparar um perfil próprio com todas as dependências identificadas e a
   fronteira fixa em `explosionFlushBlasts`. Reusar os serviços existentes.
3. Executar o caminho original recompilado e conferir o estado do bloco:
   sub-região do heap, tabelas, contadores, dados transformados, iluminação,
   retorno e liberação dos overlays 33/34.
4. Comparar o mesmo caminho com a execução MIPS, incluindo leituras da ROM,
   memória e registradores relevantes. Manter as exclusões já justificadas;
   investigar divergências antes de mudar o contrato da comparação.
5. Validar os três parâmetros de TV na ROM US, o encerramento dos workers,
   o perfil anterior afetado e o pacote Windows pelo console SSH. Conferir
   `make VERSION=us` e a identidade binária da ROM. Publicar somente fontes,
   documentação e evidências permitidas.

Se aparecer uma dependência não mapeada, identificar sua origem e atualizar
este plano antes de ampliar o escopo. Não atravessar a fronteira escolhida
nem devolver sucesso fictício para uma rotina ausente. Gráficos, input físico
e frames de áudio não entram nesta entrega.

## Caminho até uma partida

Os próximos marcos são uma ordem de investigação e integração. Seu escopo
exato será fechado com a evidência do marco anterior, sem prazo ou percentual
estimado sem base.

| Marco | Resultado verificável | Dependências e decisão |
| --- | --- | --- |
| 1. Estado inicial dos objetos — concluído | Retorno de `objInitObjects`, dados corretos e módulos temporários liberados | Pacote descrito acima; três cenários nativos Linux/Windows e MIPS aprovados |
| 2. Prova gráfica com asset real — concluída no escopo limitado | Painel e Juno em pose neutra renderizados no RT64/D3D12 da RTX; carga e pose conferidas com MIPS | Adaptador explícito F3DJFG para os dois casos; câmera controlada, sem VI, animação integrada ou ciclo do jogo. Suporte geral ao microcódigo continua pendente |
| 3. Bootstrap completo e primeiro ciclo do jogo | `mainInitGame` retorna; o caminho de mudança de fase produz uma tarefa gráfica válida e avança o ciclo | Integrar a cadeia restante em grupos de efeitos, fontes/interface e estado do jogo, usando o contrato gráfico comprovado |
| 4. Menu utilizável | Imagem estável, navegação, áudio produzido e retomada da espera entre frames | Entrada real, scheduler RSP/RDP, síntese de áudio e apresentação; a thread de áudio apenas bloqueada não satisfaz este marco |
| 5. Fase jogável | Movimento, câmera, combate, transição, salvamento e carregamento exercitados | Ampliar cobertura de objetos, animações, colisões e recursos efetivamente usados; investigar estabilidade e desempenho |

A análise e o probe do [primeiro pacote gráfico](graphics/PLAN.md) confirmaram
que o RT64 `43373749` não reconhece o microcódigo presente no snapshot real.
As listas geradas contêm comandos específicos `0x04`, `0x05` e `0x07`; os
vértices dependem de uma base DMA externa. A integração gráfica requer essa
adaptação ou uma rota RSP/RDP verificada. O [adaptador limitado](rt64/README.md)
agora comprovou dois framebuffers por readback da GPU, incluindo os lotes,
texturas e matrizes de translação da pose neutra do personagem. Isso ainda
não cobre matrizes animadas, iluminação original ou todas as listas do jogo.

Preferência atual de Thiago: manter Windows/NVIDIA, sem Android por enquanto,
e continuar somente com o agente principal após as delegações já concluídas.

## Ordem restante conhecida do bootstrap

Após o pacote atual, `mainInitRlo` chama, nesta ordem, rotinas dos grupos:

- Explosões/diagnóstico/partículas: `explosionFlushBlasts`,
  `arithmeticFunction`, `diPrintfInit`, `partInitLib`.
- Ambiente e interface: `initWeather`, auxiliares locais do overlay 36,
  `fxInitLines`, `fxInitLevelEffects`, `camlightInit`, `fontInit`.
- Estado de jogo: `packInit`, `initFront`, `animseqInit`, `fxInit`,
  `squadsInit`, `diRcpTraceInit`, `fmvInit`.
- Finalização: buffers locais, fila/cliente do scheduler, `objGetTable`,
  lista gráfica inicial, relógio e `mathSeed`, então retorno.

Há chamadas intermediárias de `mainPreNMI` e liberação de overlays, que
devem continuar sendo verificadas. O caminho posterior em `src/main.c`
passa por leitura dos controles, mudança de fase e ciclo principal.

Fontes do planejamento: ASM de `mainInitRlo` e `objInitObjects`, realocações
originais resolvidas de `tools/overlay_reloc.py`, `src/main.c` e evidências
dos perfis existentes. Dois subagentes GPT-6-Sol com esforço médio fizeram
a análise de dependências e do roteiro; o coordenador definiu o pacote e
seus critérios. Não há medição comparável de economia de tokens.
