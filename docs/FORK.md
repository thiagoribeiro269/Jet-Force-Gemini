# Direção do fork

Thiago decidiu manter este fork como um projeto independente, desenvolvido com assistência de IA. O trabalho de Ryan Myers e dos demais colaboradores permanece creditado como origem.

## Base escolhida

- Repositório: `thiagoribeiro269/Jet-Force-Gemini`.
- Commit de partida: `f409c111053c671ae91051a6cdff277e0af7d95e`.
- Título: `Decompile main and control helper functions`.
- Autor: Thiago Ribeiro.
- Contribuição aceita: [PR #2 no projeto original](https://github.com/Ryan-Myers/Jet-Force-Gemini/pull/2).
- Escopo: funções auxiliares de `src/main.c`, `controlMakeGravity`, `controlFSUvels` e declarações necessárias.

A escolha é conservar essa versão, incluindo a contribuição aceita, e continuar a partir dela. Mudanças posteriores do original dependem de uma decisão futura de Thiago.

## Critério de qualidade

Para descompilação sem alteração de comportamento, a saída US deve corresponder byte a byte à ROM base. O projeto já fornece a referência em `ver/verification/jfg.us.sha1`:

```text
493ced9008dbe932d6e91179b68e8630cf23a023
```

Fluxo de validação após configurar as ferramentas e fornecer a ROM local:

```sh
make extract VERSION=us
make VERSION=us
cmp baseroms/baserom.us.z64 build/jfg.us.z64
```

O PR original relata uma compilação equivalente. Essa evidência histórica não substitui uma nova validação local ao preparar o ambiente ou modificar o código.

### Validação inicial em 23/09/2026

A base `f409c111053c671ae91051a6cdff277e0af7d95e` foi recompilada em Linux x86-64 com as ferramentas e os submódulos definidos nesta versão:

- `make setup COLOR=0`: concluído.
- `make extract VERSION=us COLOR=0`: concluído.
- `make -j4 VERSION=us COLOR=0`: concluído com `Verify: OK` e os dois CRCs corretos.
- `cmp baseroms/baserom.us.z64 build/jfg.us.z64`: código de saída `0`, confirmando igualdade byte a byte.
- ROM base US: SHA-1 `493ced9008dbe932d6e91179b68e8630cf23a023`.

O código de jogo permaneceu na base escolhida. A preparação do fork acrescentou sua documentação e as instruções de desenvolvimento.

## Descompilação e port nativo

Este checkout recompila uma ROM para N64. Um port para outra arquitetura exige trabalho adicional na execução, nos gráficos, no áudio e nas demais interfaces com o hardware.

A recompilação estática usa referências como [N64Recomp](https://github.com/N64Recomp/N64Recomp) e [Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp). Ela não depende de concluir toda a descompilação em C legível, mas exige metadados e adaptação específicos do jogo.

Em 24/09/2026, Thiago autorizou o início do trabalho e escolheu **Windows x64 com GPU NVIDIA** como alvo. A branch `port/recomp-poc` contém a [primeira prova de CPU](../port/README.md): funções reais do programa principal e de dois overlays, com validação diferencial contra MIPS64 e contra a rotina original de realocação. A viabilidade do port completo, incluindo gráficos F3DJFG, áudio e boot, permanece em investigação.

A ampliação seguinte cobre 20 funções e chamadas dos overlays ao programa principal, além de um carregador básico com BSS e proteção contra sobreposição. Foram aprovadas 816 comparações diferenciais no Linux. A documentação do port mantém separadas a validação Windows dos primeiros binários e a evidência da versão ampliada.

O [perfil de inicialização da memória e do carregador](../port/boot/README.md) passou a executar o heap e o `runLink` originais recompilados, carregar quatro módulos e chamar a inicialização real de um deles. Foram aprovados 56 pontos de comparação com MIPS no Linux. A sequência nativa também foi executada no Windows da RTX, nos limites padrão e estendido do heap, sem emulador. Esta etapa não inicia o boot completo nem a parte gráfica.

O [adaptador de filas](../port/runtime/README.md) valida os serviços de mensagens do N64ModernRuntime separadamente, preparando a futura integração com threads e com a inicialização do jogo.

O [perfil de threads cooperativas](../port/threads/README.md) conecta filas e criação/início de threads a funções recompiladas. A rotina original `rcpWaitDP` suspende e retoma por mensagens, enquanto outra thread executa código do jogo. Os testes cobrem prioridades e fechamento de todas as threads criadas. A integração completa de `mainInitGame` e os gráficos continuam pendentes.

O [perfil de eventos e timers](../port/events/README.md) inicia o scheduler original na ordem US, antes do heap, processa notificações de VI e entrega eventos aos clientes do jogo. Os temporizadores usam relógio controlado ou monotônico do computador e pertencem à sessão. Essa execução ainda não desenha frames nem completa `mainInitGame`.

O [perfil de inicialização e PI](../port/init/README.md) passou a executar `mainInitGame` desde sua entrada, preparando buffers de vídeo, filas do RCP e tabelas lidas da ROM pelo `romCopy` original. A descompressão de um arquivo de 153.616 bytes coincide com MIPS e zlib. O percurso para explicitamente na chamada dinâmica de `mainInitRlo`, no overlay 36; o boot completo e o jogo visual continuam pendentes.

Em 26/09/2026, Thiago passou a condução do projeto ao Claude Code, mantendo
as mesmas diretrizes. O [movimento do Juno em Forest First](../port/native/MOVEMENT.md)
foi portado a partir das rotinas originais de controle, física e colisão,
lidas estaticamente e auditadas contra a ROM, sem emulação. A pedido de Thiago,
o comportamento original é a referência; câmera e entrada do port são marcadas
como provisórias até o port da câmera original e da entrada física.

## Melhorias futuras

As ideias para depois da base jogável incluem dublagem em inglês, modelos com mais detalhes e melhorias visuais. São possibilidades para uma etapa posterior; não fazem parte da prova atual de CPU. A prioridade permanece chegar a um port funcional que preserve a identidade do jogo.

## Créditos e arquivos de jogo

Preserve o histórico e a autoria das contribuições herdadas. Esta decisão de desenvolvimento não altera licenças nem atribuições existentes. A raiz desta base não contém uma licença geral explícita.

ROMs, assets extraídos e binários de jogo não são distribuídos por este repositório. A ROM fornecida localmente deve permanecer ignorada pelo Git.
