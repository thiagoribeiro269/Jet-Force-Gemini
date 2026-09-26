# Movimento, colisão e câmera originais em Forest First

O Juno agora anda, corre, pula, cai, sobe rampas e para em paredes dentro de
Forest First pelo código original portado para C++, sem emulação. A física,
a colisão com o cenário e a escolha de animações seguem as rotinas do jogo.
A câmera livre do jogo também foi portada e define a direção do controle,
como no N64. Só as mãos do jogador ainda são um roteiro: veja [Limites](#limites).

## Rotinas recuperadas

A leitura foi estática, sobre as listagens ASM do repositório. O conversor
confere 32 rotinas, 45.116 bytes, contra a ROM US antes de gerar os dados.

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
| `boyControl` e estados `0x2708`, `0x3464`, `0x4934` | `JunoBody::tick`, `walk`, `air`, `lateralDecay` | Controle, andar, ar, pulo e passo lateral |
| `0x5BB8`, `objMoveXYZ`, `controlPlatform` | `JunoBody::move`, `objMove` | Gravidade, deslocamento, colisão e velocidade real |
| `0x5120`, `objAnimDframe`, `objAnimSetMove`, `0x4F78` | `JunoBody::animate`, `requestMove` | Máquina de movimentos e posição dos clipes |
| `controlHalfTurn`, `controlWalkingBack`, `dAngle` | `halfTurn` e auxiliares | Meia-volta e limites de velocidade |
| `joyClamp`, `mathRnd`, `Sinf`, `Arctanf`, `Powerf`, rotações | `joyClamp`, `OriginalRandom`, `OriginalMath` | Primitivas numéricas originais |

As rotinas de overlay foram lidas com `overlay_listing.py`, que troca os
endereços provisórios das listagens pelos alvos das realocações da ROM. É uma
transformação de texto para o descompilador m2c; não executa código.

## Contratos observados

- Um tique do host, a 60 Hz, é um quadro original com `frames = 1`. O jogo
  aceita passos maiores; o port usa o menor, sempre fixo.
- O corpo do Juno são três esferas de raio 13 a 13, 26 e 39 unidades acima
  dos pés. Duas esferas da tabela ficam fora da colisão pela máscara 0x18.
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

## Verificação

- Linux com ASAN/UBSAN e Windows na RTX: 6 casos de matemática, 10 de colisão
  sintética, 5 de movimento e câmera com dados reais e 15 rejeições. O resumo
  do cenário, que inclui a posição da câmera a cada tique, é igual nas duas
  plataformas.
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
| 480–520 | C-left | a câmera volta para trás do Juno, com desvio lateral |
| 520–540 | nenhum | repouso |

## Limites

- O roteiro substitui as mãos do jogador para que o teste seja reproduzível.
  No jogo, a entrada vem do controle; o port já aplica o `joyClamp` original
  aos valores brutos. Não existe roteiro original equivalente.
- Da câmera, só a câmera livre do jogador no modo de colisão 1. Câmeras de
  zona, estáticas, de spline, de cena de corte e de mira falham ou ficam fora.
  O botão de mira, que leva ao estado 0xB, falha de forma explícita.
- Só os estados de andar e de ar do Juno. Água, lava e os demais estados
  falham de forma explícita quando alcançados. Agarrar bordas, esmagamento,
  armas, objetos com modelos de colisão e os outros personagens ainda não são
  executados.
- A mistura entre clipes é nativa; `controlSetTransition` não foi portado.
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
