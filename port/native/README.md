# Renderização e animação nativas para Windows

Esta é a rota ativa desde a decisão de Thiago em 25/09/2026: **sem RT64,
sem emulação e sem código copiado de emuladores**. O [plano](PLAN.md)
substitui a arquitetura dos checkpoints anteriores.

O executável C++ recebe meshes e texturas convertidos diretamente da ROM
local e desenha por Direct3D 11 com shaders HLSL próprios. Não contém
interpretador de instruções MIPS, display lists, microcódigo RSP ou comandos
RDP. Não simula CIC/PIF. A única API gráfica utilizada é a do Windows.

## Resultado observado

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
áudio, input e ciclo do jogo ainda não estão integrados.

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

## Build e teste

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

A inspeção dos imports do executável mostrou somente bibliotecas Windows:
D3D11, D3DCompiler, DXGI, Kernel32 e Universal CRT. O comando de compilação
inclui apenas `render.cpp` e essas bibliotecas; nenhuma dependência de
emulador é compilada estaticamente. As evidências estão em
[validation.json](validation.json).

## Continuidade

O port completo ainda exige integrar o código de jogo recompilado/adaptado,
seleção de animações ligada ao estado do personagem, materiais especiais, iluminação, áudio, controles, salvamento
e a inicialização. A dependência do CIC será retirada no caminho do port
após mapear os efeitos da chamada, sem implementar um chip virtual.
O código matching da ROM foi preservado.

A integração RT64 foi removida da árvore ativa, preservada somente no
histórico Git (`b1753bb`). Os pontos de execução dos antigos oráculos MIPS
foram retirados/desabilitados; seus relatórios são históricos, não testes
da nova rota. Nenhuma emulação foi executada para validar este pacote.

Implementação assistida por OpenAI Codex, pelo agente principal, sem novos
subagentes. O renderer e seus shaders são código próprio deste projeto.
