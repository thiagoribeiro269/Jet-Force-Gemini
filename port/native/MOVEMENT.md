# Movimento, colisão e câmera originais em Forest First

O Juno agora anda, corre, pula, cai, sobe rampas e para em paredes dentro de
Forest First pelo código original portado para C++, sem emulação. A física,
a colisão com o cenário e a escolha de animações seguem as rotinas do jogo.
A câmera livre do jogo também foi portada e define a direção do controle,
como no N64. A entrada chega como no console: botões e analógico brutos do
N64, lidos por `joyRead` e `controlReadJoypad`. Só as mãos do jogador ainda
são um roteiro: veja [Limites](#limites).

## Rotinas recuperadas

A leitura foi estática, sobre as listagens ASM do repositório. O conversor
confere 52 rotinas, 63.732 bytes, contra a ROM US antes de gerar os dados.

| Original | Port | Papel |
| --- | --- | --- |
| Overlay 24 `0x01800908` | `TrackCollision::computeMasks` | Ocupação de cada triângulo em 16 faixas X, 16 Z e 8 Y do bloco |
| Overlay 24 `0x01800CE4` | `TrackCollision::computePlanes` | Planos de face e de aresta, arestas expostas e bordas de apoio |
| `trackMakePolylist`, `getXZCompareMask`, `getYCompareMask` | `TrackQuery::makePolylist` | Candidatos por caixa de bloco, flags de lote e máscaras |
| `func_800182C0` | `TrackQuery::buildEdges` | Lista de arestas expostas dos candidatos |
| `func_80016EA0` | `TrackQuery::planeTest` | Esfera em movimento contra planos, com resposta de piso, parede e teto |
| `func_800175A0`, `trackCylinderIntersect`, `trackSphereIntersect` | `TrackQuery::edgeTest` | Esfera contra arestas e vértices |
| `trackGetPlayerIntersect` | `TrackQuery::playerIntersect` | Resolução iterativa das esferas do corpo |
| `trackPolyHeight`, `mathXZInTri` | `TrackQuery::polyHeight` | Superfícies especiais (água e lava) |
| `controlGroundHits`, `func_80035628`, `func_800344C8` | `JunoBody::groundHits`, `placeSpheres`, `tilt` | Contatos, travamento e inclinação |
| `boyControl` e estados `0x2708`, `0x3464` | `JunoBody::tick`, `walk`, `air` | Controle, andar, ar e pulo |
| `0x4934` | `JunoBody::strafe` | Passo lateral com C-left/C-right e seu decaimento |
| `joyRead`, `controlReadJoypad`, `controlPlayer`, `frontGetTargetControl` | `JoypadReader`, `controlReadJoypad`, `ControlModeKeys` | Leitura do controle e tabelas dos modos Normal e Expert |
| `controlUpdatePlayerAim`, `controlUpdateWeapon`, `boyCanFire` | `aimWithoutTargets`, `canFire` | Contador de tiro `+0x1F4` e decisão de disparo da pistola |
| `0x5BB8`, `objMoveXYZ`, `controlPlatform` | `JunoBody::move`, `objMove` | Gravidade, deslocamento, colisão e velocidade real |
| `0x5120`, `objAnimDframe`, `objAnimSetMove`, `0x4F78` | `JunoBody::animate`, `requestMove` | Máquina de movimentos e posição dos clipes |
| `controlSetTransition`, `controlPlayerGunWeight` | `setTransition`, remapeamento em `requestMove` | Perfil de colisão de cada movimento e variantes com arma |
| `controlHalfTurn`, `controlWalkingBack`, `dAngle` | `halfTurn` e auxiliares | Meia-volta e limites de velocidade |
| `joyClamp`, `mathRnd`, `Sinf`, `Arctanf`, `Powerf`, rotações | `joyClamp`, `OriginalRandom`, `OriginalMath` | Primitivas numéricas originais |

As rotinas `joyRead`, `joyClamp` e `controlReadJoypad` são C matching do
repositório; a igualdade delas com a ROM vem de `make VERSION=us` e `cmp`.

As rotinas de overlay foram lidas com `overlay_listing.py`, que troca os
endereços provisórios das listagens pelos alvos das realocações da ROM. É uma
transformação de texto para o descompilador m2c; não executa código.

## Contratos observados

- Um tique do host, a 60 Hz, é um quadro original com `frames = 1`. O jogo
  aceita passos maiores; o port usa o menor, sempre fixo.
- O corpo do Juno depende do movimento. Cada pedido de movimento aplica o
  perfil de colisão da linha escolhida (`controlSetTransition`), e as esferas
  migram até ele em cinco quadros. Andando, são três esferas de raio 15 a 15,
  30 e 45 unidades acima dos pés; a cabeça e a arma ficam fora pela máscara
  0x18. Só o primeiro quadro usa a tabela do `controlPlayerInit`, com raio 13.
- Com a pistola, `controlPlayerGunWeight` vale 1 para o Juno. Os movimentos
  são remapeados para as variantes com a arma em punho: parado vira o 45,
  andar o 36, correr o 35 e o 34, e o passo lateral o 47 e o 48. Enquanto o
  contador de tiro corre, vale a coluna de tiro, com o perfil 3, que mantém a
  esfera da arma.
- A gravidade é 0,45 por tique ao quadrado, das tabelas `objGetTable(1)` e
  `(2)`. O pulo correndo soma 10 à velocidade vertical; parado, o pulo é
  carregado por até 24 tiques e soma de 6 a 12.
- No piso, com normal Y de pelo menos 0,707, a esfera é projetada na vertical.
  Na parede, é empurrada na horizontal; no teto, pela normal.
- Lotes com as flags 0x880 ou 0xCE002000 ficam fora, exceto os marcados com
  0x02000000, que valem só para o Juno.
- A velocidade final vem do deslocamento real após a colisão. A conversão para
  o referencial local usa a velocidade pretendida, antes da colisão.
- As tabelas de seno e arco-tangente vêm da ROM. Ângulos fora de múltiplos de
  16 carregam o desvio de um passo do cosseno, como no jogo.
- O ramo antipirataria de `boyControl` (`arithmeticSums`) segue o caminho de
  uma ROM legítima, com escala de analógico 0,0625.

Os dados privados ficam em `build/`: `collision.bin` guarda blocos, vértices,
triângulos, lotes e vizinhanças; `juno-physics.bin` guarda esferas, tabelas,
constantes, taxas de animação, semente inicial e tabelas trigonométricas;
`juno-camera.bin` guarda as tabelas por personagem e os seis perfis da câmera.
As máscaras, os planos e as arestas expostas são calculados na carga pelo C++,
como no jogo.

## Câmera original

O conversor confere mais 18 rotinas, 19.616 bytes, contra a ROM US. A câmera
do Juno é a câmera livre do jogador, com colisão de câmera no modo 1, o modo
que o cabeçalho de Forest First indica.

| Original | Port | Papel |
| --- | --- | --- |
| `func_8002B378` | `JunoCamera::tick` | Despacho da câmera do jogador, caminho da câmera livre |
| `func_8002CF78` | `JunoCamera::freeCamera` | Perfis, distância preguiçosa, órbita, mira, olhar à frente, ângulos e empurrão |
| `func_8002CBD0` | `JunoCamera::collide` | Raio da mira até a câmera e folga de 16 unidades de teto e piso |
| `func_8002F2BC`, `func_8002F0E8` | ramo sem zona em `freeCamera` | Zonas de câmera registradas por objetos |
| `func_8002F45C` | início de `freeCamera` | Perfil do personagem escalado pelo estado |
| `func_8002EDA0`, `camInit`, `func_8003F66C` | construtor de `JunoCamera` | Posição inicial e oito quadros de acomodação |
| `camSetProjMtx`, `camSetFOV` | `JunoCamera::view` | Perspectiva de 52° a 60°, 4:3, planos 10 e 15.000 |
| `trackNearestIntersection`, `trackClip3D`, `func_80019324` | `TrackQuery::nearestIntersection`, `clip3D` | Interseção mais próxima de um segmento com o cenário |
| `trackCylinderHeights`, `func_8001A990`, `trackGetCubeBlockList` | `TrackQuery::cylinderHeights` e auxiliares | Alturas de teto e piso num cilindro |
| `levelGetCamera` | `readJunoCamera` | Modo de colisão da câmera da fase |

Comportamentos que a prova exibe:

- A câmera nasce atrás do Juno e o segue de forma preguiçosa. Entre 115 e 200
  unidades de distância horizontal ela mantém a distância atual.
- Sem C-buttons, a órbita acompanha a posição real da câmera. Ela só persegue
  as costas do Juno além de 220 unidades, em alguns estados ou com C-buttons.
  O analógico é lido em relação ao ângulo da câmera.
- C-left e C-right levam a câmera para trás do Juno com um desvio lateral de
  até 45°. Quando o Juno volta a se mover, o desvio se desfaz aos poucos.
- Num pulo de até 108 unidades, a câmera mira a altura de onde o pulo partiu.
  Por isso o Juno sai pelo alto do quadro no pico do pulo correndo.
- A câmera empurra o Juno se ficar a menos de 32 unidades e não desce mais de
  100 unidades abaixo do ponto mais baixo do cenário.

As zonas de câmera só existem quando um objeto `OverrideCamera` as registra.
O conversor lê a lista de objetos de Forest First e confirma que ela não tem
esse objeto nem câmeras estáticas. Os dois objetos `cutcamera` são câmeras de
cena de corte, fora deste escopo.

## Controle original

O port recebe, a cada tique, o estado do controle do N64: 16 bits de botões e
o analógico bruto. `joyRead` calcula os botões pressionados e soltos, e
`controlReadJoypad` aplica o `joyClamp` e zera tudo enquanto o controle está
desabilitado, como na trava de pouso. A câmera lê as mesmas chaves depois do
personagem, então também não gira durante essa trava.

O jogo escolhe a tabela de botões pela opção do menu. Em jogo novo ela é a
Normal; a Expert também foi convertida.

| Botão (Normal) | Estado 0, andar | Estado 3, ar |
| --- | --- | --- |
| A | pulo; com passo lateral, pulo correndo | segurar carrega o pulo |
| C-left/C-right | passo lateral e giro da câmera | só a câmera |
| R | mira, estado 0xB: **parada** | a câmera se alinha atrás do Juno |
| B | agachar, estados 1 e 2: **parada** | ignorado pelo Juno |
| Z | tiro da pistola: **parada** quando `boyCanFire` permite | ignorado: no ar não há tiro |
| C-up/C-down, D-pad | trocar arma: inerte com uma arma só | inerte |
| L, Start | sem leitura no personagem | sem leitura |

Uma **parada** interrompe o tique com `NotPortedError` e preserva o último
mundo confirmado. Z tem um caso sem parada: durante uma derrapagem ou
meia-volta, `boyCanFire` recusa o tiro, e o contador `+0x1F4` só impede a
meia-volta e troca o repouso sorteado pelo movimento 0x12, como no original.

Bordas não existem no cenário de Forest First. As marcas de borda
(`+0x20`, bits 3 a 5) só vêm de faces com os bits 0 ou 1, e nenhuma face da
fase os tem. O conversor, a carga em C++ e `check_movement` exigem isso, então
`controlHangOK` e `controlGrabOK` não encontrariam borda no cenário.

Pela leitura estática das rotinas, sem teste automático, outras três
checagens de cada tique também não agem nesse recorte:
`controlSquashCheckPrior/Post` dependem de caixas, plataformas ou estados de
borda; `0x2220` trata acertos no Juno; `controlFadePlayer` só age na mira.

## Verificação

- Linux com ASAN/UBSAN e Windows na RTX: 6 casos de matemática, 4 de entrada,
  10 de colisão sintética, 10 de movimento e câmera com dados reais, 5 paradas
  tipadas e 18 rejeições. O resumo do cenário, que inclui a posição da câmera
  a cada tique, é igual nas duas plataformas.
- Os testes cobrem o passo lateral de 0,2 por quadro até 2,5, o pulo correndo
  durante o passo lateral, o modo Expert, os botões inertes e a trava de pouso.
- Na carga de Forest First surgem 5.759 planos e 1.759 arestas expostas. Todos
  os planos são normalizados e todas as referências são válidas.
- O Juno nasce no ponto original, 21 unidades acima do caminho, e pousa em
  Y = −1,99 no décimo tique.
- A sessão e o par corpo e câmera isolados terminam no mesmo estado, bit a
  bit, após o roteiro inteiro. Pausa, retomada e rejeições preservam o mundo.
- Em todo tique, a câmera fica a pelo menos 32 unidades do Juno e acima do
  limite do cenário.
- A RTX produziu 270 quadros, todos distintos, em 640 × 480, nove segundos a
  30 fps.
  As sete provas gráficas anteriores, incluindo a da região, ficaram idênticas
  byte a byte. A ROM matching continua com SHA-1
  `493ced9008dbe932d6e91179b68e8630cf23a023`.

Métricas e hashes estão em [movement-validation.json](movement-validation.json).

## Roteiro da prova

| Tiques | Comando | Resultado |
| --- | --- | --- |
| 0–30 | nenhum | queda do ponto original até o caminho |
| 30–330 | analógico para frente | corrida para o sul e subida da encosta até Y ≈ 58 |
| 330–340 | pulo correndo | salto a partir do alto da encosta |
| 340–460 | analógico para trás | meia-volta original e corrida em direção à câmera |
| 460–480 | nenhum | desaceleração |
| 480–520 | C-left | passo lateral para a esquerda; a câmera volta para trás do Juno |
| 520–540 | nenhum | repouso |

## Limites

- O roteiro substitui as mãos do jogador para que o teste seja reproduzível.
  Ele entrega valores brutos do N64 ao mesmo caminho de leitura do jogo. Não
  existe roteiro original equivalente.
- Da câmera, só a câmera livre do jogador no modo de colisão 1. Câmeras de
  zona, estáticas, de spline, de cena de corte e de mira falham ou ficam fora.
  O botão de mira, que leva ao estado 0xB, falha de forma explícita.
- Só os estados de andar e de ar do Juno. Mira, agachar, tiro, água, lava e
  os demais estados param de forma explícita quando alcançados. Objetos com
  modelos de colisão e os outros personagens ainda não existem.
- O modelo da pistola não é desenhado: a pose das variantes com arma
  aparece com as mãos vazias.
- As rotações de juntas não foram portadas. No original, o tronco do Juno
  gira durante o passo lateral (`0x4840`) e a cabeça acompanha a mira
  (`0x6290`); no port, o corpo segue só a animação.
- A mistura entre clipes é nativa. A parte de colisão de `controlSetTransition`
  foi portada; a interpolação visual entre clipes continua própria do port.
- Não há inimigos, áudio nem `levelInit` completo.

## Reprodução

```sh
python3 port/native/prepare_assets.py --movement --out build/port-native/movement
python3 port/native/build.py
python3 port/native/package.py --out build/port-native/movement
```

Executar o pacote privado no Windows autorizado com `run_windows.py`, como em
[REGION.md](REGION.md#reprodução), recuperar `result.json` como
`build/port-native/movement/rtx-result.json` e os arquivos `.rgba` gerados, e
então rodar `python3 port/native/check_movement_frames.py`.
