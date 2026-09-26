# Movimento e colisão originais em Forest First

O Juno agora anda, corre, pula, cai, sobe rampas e para em paredes dentro de
Forest First pelo código original portado para C++, sem emulação. A física,
a colisão com o cenário e a escolha de animações seguem as rotinas do jogo.
A câmera e o controle ainda não são os originais: veja [Limites](#limites).

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
constantes, taxas de animação, semente inicial e tabelas trigonométricas. As
máscaras, os planos e as arestas expostas são calculados na carga pelo C++,
como no jogo.

## Verificação

- Linux com ASAN/UBSAN e Windows na RTX: 6 casos de matemática, 7 de colisão
  sintética, 4 de movimento com dados reais e 12 rejeições. O resumo do
  cenário é igual nas duas plataformas.
- Na carga de Forest First surgem 5.759 planos e 1.759 arestas expostas. Todos
  os planos são normalizados e todas as referências são válidas.
- O Juno nasce no ponto original, 21 unidades acima do caminho, e pousa em
  Y = −1,99 no décimo tique.
- A sessão e o corpo isolado terminam no mesmo estado, bit a bit, após o
  roteiro inteiro. Pausa, retomada e rejeições preservam o mundo.
- A RTX produziu 270 quadros distintos em 640 × 480, nove segundos a 30 fps.
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
| 340–460 | analógico para trás | meia-volta original e corrida de volta |
| 460–540 | nenhum | desaceleração e repouso |

## Limites

- A câmera da prova é uma política do port. O sistema de câmera original é o
  próximo trabalho.
- O roteiro substitui as mãos do jogador para que o teste seja reproduzível.
  No jogo, a entrada vem do controle; o port já aplica o `joyClamp` original
  aos valores brutos.
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
