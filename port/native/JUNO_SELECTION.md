# Seleção original de animações do Juno

Este pacote porta para C++ duas decisões recuperadas por leitura estática
do código do jogo. Os dados são convertidos da ROM local, e o resultado
alimenta o renderer D3D11 próprio. Não há execução de MIPS, representação
de RAM do console, interpretação de comandos gráficos ou código de emulador.

## Origem e fronteira

| Fonte original | Implementação nativa | Escopo |
| --- | --- | --- |
| Overlay 16, `0x4E08` | `JunoSelector::choose` | Índice de movimento a partir de dois componentes e campos de estado |
| Overlay 16, `0x4F78` | `JunoSelector::resolve` | Remapeamento e escolha do perfil de transição |
| `objAnimSetMove`, `0x8001149C` | `AnimationPlayer::selectAt` | Fração inicial do clipe no subconjunto convertido |
| `controlPlayerGunWeight`, `0x8003B114` | Entrada booleana explícita | A consulta à arma/personagem ainda pertence ao chamador |
| `controlSetTransition`, `0x80035514` | Índice propagado como metadado | Conteúdo e aplicação do perfil ainda não portados |

O conversor confere 1.700 bytes das cinco rotinas contra os comentários de
instruções nos respectivos arquivos ASM, cinco realocações de chamadas e
dez de dados. Essa auditoria confirma a origem usada na análise; não é
execução diferencial nem prova automática de equivalência do C++.

Os índices locais estão em 52 linhas de cinco bytes em `data+0x280` do
overlay 16, ROM US `0x1F25888`. A associação a IDs globais vem das seções
`0x28/0x29` do modelo 220. O limiar de repouso está em `rodata+0x144`, ROM
`0x1F25F7C`, com o valor float32 de 0,1. Os bytes das tabelas ficam somente
no arquivo privado `juno-selection.bin` em `build/`.

## Escolha do movimento

`component04` e `component10` representam os floats nos offsets `+0x04` e
`+0x10` do estado original. Não são eixos normalizados de controle nem foram
convertidos para unidades por segundo. Nomes por offset preservam as
incertezas sobre campos cujo significado completo ainda não foi estabelecido.

1. Calcular o maior valor absoluto dos dois componentes.
2. Abaixo de 0,1: `+0x1FA` não zero escolhe 16; depois, qualquer um de
   `+0x1F9`, `+0x1F4` ou `+0x198` não zero escolhe 18; caso contrário, usar
   o resultado de `mathRnd(16,19)`. A faixa inclusiva foi conferida em
   `src/hasm/ido/math_util.s`; o port recebe o resultado, sem simular o RNG.
3. Fora desse repouso, se o absoluto de `+0x10` for estritamente maior,
   escolher 9 quando ele é negativo, ou 10 quando é positivo.
4. Nos demais casos, `+0x569` não zero escolhe 3. Esse campo também é usado
   pela rotina `controlWalkingBack`.
5. Acima de 3,5 escolher 2; acima de 1,75 escolher 1; caso contrário, 0.

As comparações são estritas: exatamente 0,1 já entra no caminho de movimento;
1,75 permanece no índice 0 e 3,5 no índice 1. Empate entre os absolutos não
entra no ramo lateral. A prova cobre valores imediatamente abaixo/acima dos
limiares com `nextafter`, sinais e prioridades dos campos.

## Remapeamento por contexto

Cada linha possui três destinos e dois perfis. A escolha do perfil usa a
linha **do destino**, depois do remapeamento:

| Condição, por prioridade | Coluna de destino | Coluna de perfil no destino |
| --- | --- | --- |
| Objeto presente em `+0x5CC` | 2 | 3 |
| `+0x1F4` não zero e resultado de `controlPlayerGunWeight` verdadeiro | 1 | 4 |
| Resultado de `controlPlayerGunWeight` verdadeiro | 2 | 3 |
| Demais estados | 0 | 3 |

O papel de `+0x5CC` foi cruzado com `controlEmptyPlayersHand`. O campo
`+0x1F4` participa de `controlUpdatePlayerAim`, onde também é decrementado;
por isso não o renomeamos simplesmente como um botão de mira. A rotina
`controlPlayerGunWeight` consulta um bit da tabela da arma para o personagem;
a prova fornece esse resultado como entrada, sem implementar inventário.

O seletor aceita a tabela completa. O renderer deste pacote converte apenas
nove clipes: os três anteriores, mais 1027, 1028, 1025, 1019, 1055 e 1061.
Um destino conhecido na tabela, mas sem clipe convertido, falha antes de
alterar a animação. Não há substituição silenciosa por um clipe genérico.

## Fração inicial e mistura

O argumento passado a `objAnimSetMove` é limitado a `[0,1]` e multiplicado
pelo intervalo de quadros do clipe. No subconjunto aceito, esse intervalo é
o número de quadros nos clipes em loop, ou esse número menos um nos demais.
Em `selectAt`, fração 1 de um loop amostra o início; a de um clipe sem loop
amostra o último quadro. Entradas não finitas são rejeitadas na fronteira PC.

Isso é separado de `blendSeconds`, a mistura nativa já existente. A ponte
mantém a pose visível no início da troca, e uma solicitação do mesmo clipe
preserva o relógio e a mistura. O perfil de transição pode mudar mesmo sem
trocar o clipe, como no original; ele é propagado, mas ainda não aplicado.
Não se afirma que smoothstep seja o algoritmo de `controlSetTransition`.

O clipe 1071 usado antes como repouso era uma escolha provisória. Esta
rotina original solicita os índices 16–19 nas condições de repouso. O vídeo
agora usa o 1019, índice 16, com 50 quadros, sem loop. Isso não altera a
prova anterior: seu vídeo e sua política ficam preservados para regressão.

## Verificação observada

- Linux ASAN/UBSAN e Windows: 18 casos de escolha, oito combinações de
  contexto, oito casos da ponte, 19 rejeições e 20 seleções reais revisadas
  na tabela passaram. Os testes de animação e personagem anteriores também.
- GPU NVIDIA/D3D11: 180 frames, sete clipes exercitados, 167 imagens distintas,
  dez solicitações e oito trocas. Sem salto instantâneo da pose nas trocas.
- No frame 150, fração inicial 0,5 do clipe de 50 quadros produz fase 24,5.
  A mistura dura separadamente 0,2 segundo. A repetição no frame 168 mantém
  fase 33,5, em vez de reiniciar a animação.
- Personagem inteiro na câmera em todos os frames. Inspecionados repouso,
  movimentos e variantes de braços; armas/objetos não estão sendo desenhados.
- Pose neutra, ciclo de 33 frames, transições de 96 frames e deslocamento
  de 180 frames anteriores continuam idênticos byte a byte.
- As quatro cenas anteriores também foram reconvertidas sem mudar bytes.
  `make VERSION=us COLOR=0` e comparação da ROM permanecem uma validação
  separada do port.

A evidência consolidada está em [juno-selection-validation.json](juno-selection-validation.json).
O vídeo privado tem seis segundos a 30 fps. A reprodução usa a velocidade
controlada anterior de 15 quadros de origem por segundo e não comprova a
cadência original do jogo.

## Continuidade

Ainda faltam o ciclo de `boyControl`, a origem/cadência desses estados e
solicitações, RNG, relógio original, aplicação dos perfis, física, colisão,
cenário, armas, áudio e controle físico. O diagnóstico anterior de posição
X/Z continua sendo uma política própria; não foi apresentado como física
original depois desta ligação de animações.

O próximo bloco deve mapear o avanço/término dos clipes na rotina do overlay
16 em `0x5120` e os consumidores dos perfis de `controlSetTransition`, para
substituir progressivamente o relógio e os parâmetros de teste.
