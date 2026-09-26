# Renderização e animação nativas para Windows

Esta é a rota ativa desde a decisão de Thiago em 25/09/2026: **sem RT64,
sem emulação e sem código copiado de emuladores**. O [plano](PLAN.md)
substitui a arquitetura dos checkpoints anteriores.

A prioridade agora é a [estrutura geral do port](ARCHITECTURE.md), com mapa
do programa, sessão de aplicação, cenas, entidades e backend gráfico
separados. As provas abaixo passaram a servir como regressões desse backend.
A região original [Forest First](REGION.md) já foi integrada ao host com
o Juno, que agora se move pelo [código original portado](MOVEMENT.md). O
[roteiro vigente](../ROADMAP.md) prevê a câmera original como próximo marco.

O executável C++ recebe meshes e texturas convertidos diretamente da ROM
local e desenha por Direct3D 11 com shaders HLSL próprios. Não contém
interpretador de instruções MIPS, display lists, microcódigo RSP ou comandos
RDP. Não simula CIC/PIF. A única API gráfica utilizada é a do Windows.

## Resultado observado

O checkpoint mais recente move o **Juno em Forest First** pelo controle,
gravidade, pulo, colisão e máquina de movimentos originais, portados para
C++. A RTX produziu 270 frames em nove segundos: queda do ponto de entrada,
corrida, subida da encosta, pulo, meia-volta e repouso. As sete provas
anteriores ficaram idênticas byte a byte. Câmera e roteiro de entrada ainda
são do port; detalhes em [MOVEMENT.md](MOVEMENT.md).

O checkpoint anterior desenha **Forest First, nível 21**, e Juno na
mesma sessão nativa: 2.018 triângulos de cenário, 44 texturas de terreno,
câmera em perspectiva e animação de repouso. A RTX produziu 180 frames
em seis segundos, mantendo as seis provas anteriores idênticas byte a byte.
O personagem ainda não percorre a região; câmera, alinhamento dos pés e
materiais usam as políticas nativas documentadas em [REGION.md](REGION.md).

O personagem Juno foi renderizado na RTX 5070 Ti, Windows x64, a 640 × 480:
corpo 220 e mão 309, 534 triângulos, 17 texturas e 21 chamadas de desenho.
O readback retornou 36.802 pixels coloridos e 23.080 cores RGB. A imagem foi
inspecionada com rosto, mão e visor presentes. O PNG é opaco, sem confundir
alfa interno com transparência da imagem final.

A conversão confere a ROM US por SHA-1, limites dos diretórios e assets,
expansão RZIP, contagens, índices, ossos e ranges dos lotes. A pose neutra
é montada a partir da hierarquia original, e o registro do corpo indica o
osso 6 para a mão. Os lotes marcados como ocultos nos dados continuam fora
da malha desenhada. As texturas recebem uma conversão offline da disposição
de bytes para RGBA8 comum, sem execução de comandos do console.

A aparência usa materiais convencionais do PC: cor de vértice, textura,
recorte por alfa, transparência e filtragem linear. A última componente
do vértice original não é tratada como opacidade de material. Os efeitos
originais mais complexos ainda precisam de materiais/shaders próprios;
não se afirma equivalência de pixels com o N64. A câmera é controlada;
áudio, dispositivos de entrada e ciclo original do jogo ainda não estão integrados.

## Primeiro clipe de animação

A cena `JFGNAT2` guarda vértices locais, osso de cada vértice, hierarquia
e quadros de animação. O conversor seleciona a primeira entrada da tabela
do Juno: clipe **1026**, com 16 quadros, entre 52 clipes associados ao
modelo. Seus dados originais têm 193 bits por quadro e stride de 25 bytes.
O remapeamento de canais e a presença de escala são conferidos; esta prova
aceita somente esse clipe sem canais de escala.

A interpretação do formato foi obtida por leitura estática do carregador
original e de `src/hasm/gen_anim_data.s`. Não foi executado MIPS nem usado
código de emulador. Dois leitores de bits independentes concordaram sobre
os valores extraídos. Os quadros convertidos são dados privados de entrada,
não imagens de animação pré-renderizadas.

Durante a execução Windows, `animation.h` interpola os ângulos pelo caminho
curto, compõe rotações X/Y/Z, propaga a hierarquia e transforma a malha em
C++. O vértice da mão usa o mesmo osso 6 do encaixe original. O buffer de
vértices D3D11 é atualizado a cada amostra antes do desenho.

Resultados na RTX:

- 33 frames renderizados: 32 amostras distintas e um frame de fechamento.
- Primeiro e último frames idênticos byte a byte, sem deriva no loop.
- Pose neutra da nova hierarquia idêntica ao framebuffer estático anterior.
- 41.899 pixels RGB diferentes entre início e metade do ciclo.
- Personagem inteiro dentro da câmera em todos os frames; mãos e membros
  acompanham o movimento em inspeção visual.
- Seis testes matemáticos e quatro entradas inválidas passaram no Linux
  com ASAN/UBSAN e no Windows, sem emulação.

A reprodução é controlada: meio quadro de origem por amostra a 30 fps,
equivalente a 15 quadros de origem por segundo. Isso não estabelece a
velocidade original do jogo. O vídeo privado tem três repetições do ciclo,
96 frames e 3,2 segundos. Não há blending de clipes, IK/mira, eventos,
colisão ou controle do personagem nesta prova. As evidências ficam em
[animation-validation.json](animation-validation.json).

## Seleção e transição nativas

A cena `JFGNAT3` acrescenta o clipe **1030**, índice 14 da tabela do Juno,
com dez quadros, stride de dez bytes e 77 bits por quadro. O conversor
continua limitado aos dois clipes inspecionados, sem escala animada.

`AnimationPlayer` recebe seleção por ID e avanço de tempo em segundos.
Durante a transição, mistura a pose visível capturada na origem com o
clipe destino em andamento, usando curva smoothstep e caminho angular
curto. Outra seleção durante a mistura começa da pose que estava visível.
Pedidos repetidos para o mesmo ID preservam o relógio e a transição; IDs ou
tempos inválidos falham antes de alterar o estado.

Isso é um controlador próprio do port. Não se afirma que seja o algoritmo
de mistura original do jogo, nem que preserve velocidade ou contato dos
pés com o chão. IK, foot planting, eventos e transições acionadas pela
lógica original de gameplay continuam pendentes.

O teste [transition_sequence.txt](transition_sequence.txt) envia cinco
comandos em 96 frames: quatro trocas efetivas e uma repetição ignorada.
Uma troca interrompe uma mistura em curso. Resultados observados:

- Oito casos do controlador e seis rejeições passaram no Linux ASAN/UBSAN
  e no Windows, além dos testes matemáticos anteriores.
- Diferença máxima das matrizes antes/depois de cada seleção: zero.
- 72 imagens distintas na sequência; personagem completo no enquadramento.
- Pose neutra e ciclo anterior de 33 frames continuam idênticos byte a byte.
- Os primeiros 32 frames do controlador e os frames de duas transições
  concluídas coincidem com a reprodução isolada do clipe correspondente.

A primeira tentativa cortou os pés em uma pose intermediária. A sequência
agora usa uma câmera fixa mais ampla, e um ciclo isolado com essa câmera
serve de referência para as comparações. A câmera anterior permanece nas
regressões. O vídeo privado tem 3,2 segundos, 640 × 480 e 30 fps. Os comandos
são de diagnóstico; teclado e controle físico não são acessados. Evidências
em [transition-validation.json](transition-validation.json).

## Estado e deslocamento do personagem

`CharacterController` acrescenta posição X/Z, orientação e estados nativos
de repouso, movimento e postura baixa. A entrada contém dois eixos entre
−1 e 1 e um booleano de postura baixa. O diagnóstico entrega esses comandos
por arquivo, sem acessar teclado, controle físico ou área de trabalho.

O clipe **1071**, índice 51, foi convertido para o repouso provisório: três
quadros iguais, sem loop, stride e consumo de bits zero, com rotações
constantes não nulas. A inspeção visual mostrou Juno em pé, com as mãos
reunidas à frente. Isso é uma pose do jogo, diferente da pose neutra do
esqueleto; seu significado original ainda não foi identificado.

| Entrada | Estado do protótipo | Clipe | Deslocamento |
| --- | --- | --- | --- |
| Eixos dentro da zona morta | Repouso | 1071 | Parado |
| Eixos ativos | Movimento | 1026 | Plano X/Z |
| Postura baixa pressionada | Postura baixa | 1030 | Parado, com prioridade sobre os eixos |

Esta política pertence ao protótipo do port. A leitura estática de
`objAnimSetMove` e do seletor do overlay 16 em `0x4F78` confirmou seleção
por índice local e remapeamentos dependentes do estado. Não estabeleceu
que os três nomes acima ou seus parâmetros sejam os originais do jogo.

Parâmetros controlados: 60 unidades de origem por segundo, zona morta
radial de 0,15, mistura de 0,25 segundo e giro limitado a π radianos por
segundo. Diagonais são normalizadas; entradas analógicas menores mantêm
sua intensidade fora da zona morta. Os eixos são relativos ao mundo, com
frente local −Z. Ao parar, posição e orientação são mantidas. O movimento
muda de direção imediatamente, enquanto o corpo gira gradualmente.

A translação raiz do clipe continua local ao esqueleto. O controlador não
a acumula na posição do personagem. As matrizes dos ossos recebem a
transformação do personagem uma única vez. A posição Y do personagem é
zero, mas não há chão, gravidade ou contato físico dos pés implementados.
Os tempos de animação ainda usam 15 quadros de origem por segundo, sem
ajuste da passada à velocidade; deslizamento dos pés pode ocorrer.

Comandos que mantêm o estado preservam o relógio do clipe, inclusive quando
mudam a direção. Mudanças de estado reaproveitam a transição interrompível
já validada. Entradas não finitas, eixos fora dos limites e passos de tempo
fora de `[0; 0,25]` segundo são rejeitados antes de alterar o estado.

Resultados da [sequência reproduzível](character_sequence.txt) na RTX:

- 180 frames, seis segundos a 30 fps, com 149 imagens distintas.
- 13 comandos e dez mudanças de estado, incluindo interrupção e retorno
  à pose de repouso. Nenhum salto instantâneo da pose ou posição.
- Trajeto de 186 unidades, com dez posições conferidas analiticamente.
  Movimento diagonal mantém a velocidade; repouso e postura baixa param.
- Personagem inteiro no enquadramento em todos os frames. Inspeção de
  poses de frente, perfil e postura baixa confirmou a articulação da mão.
- Pose neutra, ciclo anterior de 33 frames e transições de 96 frames
  continuam idênticos byte a byte.
- Dez casos do controlador e 20 rejeições passaram no Linux ASAN/UBSAN e
  no Windows, além das verificações de animação anteriores. O teste compara
  comandos nos mesmos instantes a 30 e 60 atualizações por segundo.

O último trecho dura 0,4 segundo, suficiente para girar 72° a partir de
90°. O personagem para a 18° e mantém essa direção, conforme a regra de
giro limitado; não é um ajuste posterior da câmera. As evidências estão em
[character-validation.json](character-validation.json).

Ainda não é uma partida nem o controle original do JFG. A integração às
rotinas de gameplay, cenário/colisão, gravidade, câmera jogável, áudio,
eventos, IK e entrada física permanece pendente.

## Seleção recuperada do código original

O [mapeamento das rotinas do Juno](JUNO_SELECTION.md) acrescenta duas
decisões portadas para C++: escolher o índice de movimento pelos componentes
e campos de estado, e remapeá-lo conforme o contexto da arma/objeto na mão.
A tabela original de 52 movimentos é convertida para um arquivo privado
do PC; o renderer dispõe de nove clipes e rejeita destinos ainda não
convertidos antes de alterar a animação.

O novo repouso usa o clipe 1019, solicitado pelo caminho original analisado.
O 1071 do diagnóstico anterior continua documentado como escolha provisória.
As três faixas de movimento, o retorno e duas variantes de contexto foram
renderizados na RTX, com 167 imagens distintas em 180 frames. O vídeo é no
lugar: ainda não associa os componentes originais à física ou posição X/Z.

`AnimationPlayer::selectAt` separa a fração inicial do clipe do tempo de
mistura nativa. A ponte preserva o relógio em solicitações repetidas e
propaga o perfil original de transição, cujo conteúdo ainda não foi portado.
Testes no Linux ASAN/UBSAN e Windows, auditoria estática e quatro regressões
gráficas passaram. Detalhes em [juno-selection-validation.json](juno-selection-validation.json).

## Build e teste

Para o checkpoint atual do movimento:

```sh
python3 port/native/prepare_assets.py --movement --out build/port-native/movement
python3 port/native/build.py
python3 port/native/package.py --out build/port-native/movement
```

O pacote inclui `jfg_native_movement.exe`, `check_movement.exe` e todas as
regressões anteriores. Reprodução e validação em [MOVEMENT.md](MOVEMENT.md#reprodução).

Para o checkpoint da região:

```sh
python3 port/native/prepare_assets.py --region --out build/port-native/region
python3 port/native/build.py
python3 port/native/package.py --out build/port-native/region
```

O pacote privado inclui `jfg_native_region.exe`, testes e regressões do
mesmo host. Instruções da execução Windows, recuperação dos frames e
validação estão em [REGION.md](REGION.md#reprodução).

Para preparar e testar a sessão de integração:

```sh
python3 port/native/prepare_assets.py --integration --out build/port-native/integration
python3 port/native/build.py
python3 port/native/package.py --out build/port-native/integration
```

O pacote inclui `jfg_native_session.exe` e os diagnósticos anteriores.
`run_windows.py` executa os testes de sessão e as cinco regressões gráficas,
sempre por console, sem janela ou acesso a dispositivos de entrada.
Recuperar `result.json` como `rtx-result.json` e os arquivos `frame.rgba`,
`neutral.rgba`, `cycle.rgba`, `cycle-wide.rgba`, `transitions.rgba`,
`character.rgba` e `juno.rgba` para `build/port-native/integration`, e rodar:

```sh
python3 port/native/check_integration_frames.py
```

O vídeo privado é `native-session.mp4`. A prova inclui duas cenas de teste,
entidades independentes, pausa, troca de cena, criação e remoção. Os testes
CPU usam 12 instantes comuns para comparar apresentação a 30/60/144 Hz;
isso não mede desempenho nem estabelece a cadência original do JFG.

O inventário estático do programa usa somente leitura do ELF e da ROM:

```sh
# Em um ambiente Python com analysis-requirements.txt instalado:
python port/native/program_map.py
```

Nesta máquina, o Python existente em `build/port-recomp/.venv/bin/python`
já fornece `pyelftools==0.32`. A ferramenta nova não importa runners antigos
nem executa MIPS. A síntese pública e os contratos estão em
[ARCHITECTURE.md](ARCHITECTURE.md); o grafo completo é gerado em `build/`.

O conversor usa apenas a biblioteca padrão do Python. O build usa o
LLVM-MinGW já instalado localmente e as bibliotecas Direct3D do Windows.

```sh
python3 port/native/prepare_assets.py
python3 port/native/build.py
python3 port/native/package.py
```

Para preparar o clipe e o pacote animado:

```sh
python3 port/native/prepare_assets.py --animation --out build/port-native/animation
python3 port/native/build.py
python3 port/native/package.py --out build/port-native/animation
```

Para preparar a seleção e as transições:

```sh
python3 port/native/prepare_assets.py --transitions --out build/port-native/transitions
python3 port/native/build.py
python3 port/native/package.py --out build/port-native/transitions
```

Para preparar o controlador do personagem com três clipes:

```sh
python3 port/native/prepare_assets.py --character --out build/port-native/character
python3 port/native/build.py
python3 port/native/package.py --out build/port-native/character
```

Para preparar o seletor recuperado e seus nove clipes:

```sh
python3 port/native/prepare_assets.py --juno-selection --out build/port-native/juno-selection
python3 port/native/build.py
python3 port/native/package.py --out build/port-native/juno-selection
```

O ZIP em `build/port-native` contém assets privados do jogo. **Não publicar
esse pacote, a ROM, as texturas, malhas ou imagens.** Somente os fontes e
as métricas de validação entram no GitHub.

Após transferir e extrair o pacote em uma pasta própria do Windows:

```powershell
python -I -O package/run_windows.py --package package --out result
```

O runner confere hashes e cria somente seu processo de console, sem janela
ou swapchain. Há um prazo de 90 segundos; em timeout ele encerra apenas
esse filho. O renderer exige uma GPU NVIDIA real e não aceita dispositivo
de software. O readback sincroniza pela API D3D11. Processos preexistentes
permanecem preservados.

Para cenas `JFGNAT2`, o runner executa primeiro os testes matemáticos,
renderiza o ciclo e produz também `neutral.rgba` para regressão. O executável
aceita `--animate` explicitamente; sem essa opção, desenha a pose neutra.

Com `JFGNAT3`, o runner também confere o controlador e executa
`--sequence transition_sequence.txt`. Além de `frame.rgba`, gera
`cycle.rgba` na câmera anterior, `cycle-wide.rgba` na câmera ampla e
`neutral.rgba`. O arquivo de comandos é limitado em tamanho, frames e
quantidade de seleções; IDs são conferidos antes de criar o dispositivo.

Recupere `result.json` como `build/port-native/rtx-result.json` e
`frame.rgba` no mesmo diretório, depois execute:

```sh
python3 port/native/check_frame.py
```

Para animação, recupere os arquivos em `build/port-native/animation`,
incluindo `neutral.rgba`, e rode `python3 port/native/check_animation_frames.py`.
O verificador confere fechamento do loop e regressão, gera PNGs de amostra
e usa FFmpeg local para criar `juno-animation.mp4`. Frames e vídeo ficam
privados e nunca devem ser incluídos no pacote público do código.

Para transições, recupere `result.json` como `rtx-result.json` e os quatro
arquivos RGBA para `build/port-native/transitions`, depois execute
`python3 port/native/check_transition_frames.py`. O vídeo resultante é
`juno-transitions.mp4`. Os traços de estado ficam no resultado privado;
o controlador não grava keyframes ou imagens no repositório público.

O perfil `--character` continua usando `JFGNAT3`, agora com três clipes;
cenas anteriores continuam aceitas. O runner executa os testes de
`check_character.exe` e usa `--character character_sequence.txt`. Recuperar
`frame.rgba`, `transitions.rgba`, `cycle.rgba`, `cycle-wide.rgba`,
`neutral.rgba` e `result.json` (como `rtx-result.json`) para
`build/port-native/character`. A validação local é:

```sh
python3 port/native/check_character_frames.py
```

Ela verifica comandos, distâncias, giro, poses estáveis, enquadramento e
regressões, e produz `juno-character.mp4` privado. Frames começam em tempo
zero: comandos do frame são aplicados antes do desenho; o avanço de 1/30
segundo ocorre depois. O controlador rejeita passos acima de 0,25 segundo;
uma integração futura precisa subdividir intervalos maiores explicitamente.

O perfil `--juno-selection` usa `JFGNAT3` com nove clipes e acrescenta
`juno-selection.bin`, no formato próprio `JFGSEL1`: cabeçalho de 16 bytes
e 52 registros de oito bytes com destinos, perfis e ID do clipe. O runner
confere também `check_juno_selection.exe` e executa
`--juno-selection juno_sequence.txt juno-selection.bin` no renderer.
Recuperar os arquivos de frames anteriores, mais `character.rgba`, para
`build/port-native/juno-selection` e executar:

```sh
python3 port/native/check_juno_frames.py
```

O vídeo é `juno-selection.mp4`, privado. Os campos de estado e as escolhas
do RNG são entradas controladas do diagnóstico, não leitura de um controle.
Os comandos são conferidos antes da criação do dispositivo D3D11.

A inspeção dos imports do executável mostrou somente bibliotecas Windows:
D3D11, D3DCompiler, DXGI, Kernel32 e Universal CRT. O comando de compilação
inclui apenas `render.cpp` e essas bibliotecas; nenhuma dependência de
emulador é compilada estaticamente. As evidências estão em
[validation.json](validation.json).

## Continuidade

Os seletores de movimento e remapeamento já alimentam entidades da sessão
nativa. O próximo bloco é integrar uma região original, seguindo
`levelInit → trackInit → objetos`. O avanço/término dos clipes em
`overlay 16+0x5120` e os consumidores de `controlSetTransition` continuam
pendências dentro dessa integração. Cadência, mistura e movimento dos
diagnósticos ainda são políticas próprias.

O port completo ainda exige integrar o código de jogo recompilado/adaptado,
materiais especiais, iluminação, áudio, controles, salvamento e a
inicialização. A dependência do CIC será retirada no caminho do port
após mapear os efeitos da chamada, sem implementar um chip virtual.
O código matching da ROM foi preservado.

A integração RT64 foi removida da árvore ativa, preservada somente no
histórico Git (`b1753bb`). Os pontos de execução dos antigos oráculos MIPS
foram retirados/desabilitados; seus relatórios são históricos, não testes
da nova rota. Nenhuma emulação foi executada para validar este pacote.

Implementação assistida por OpenAI Codex, pelo agente principal, sem novos
subagentes. O renderer e seus shaders são código próprio deste projeto.
