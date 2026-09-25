# Forest First no host nativo

O nível 21, geometria 17, foi convertido da ROM local e desenhado com Juno
pela mesma `NativeSession` e pelo backend D3D11 dos diagnósticos anteriores.
A execução Windows x64 na RTX produziu seis segundos em 640 × 480, sem
janela, acesso a dispositivos de entrada ou emulação. É uma integração de
cenário e personagem; a região ainda não está jogável.

## Recursos e integração

O conversor confere a ROM US e os bytes de nove rotinas usados na análise
estática. O cabeçalho real do nível tem 0x118 bytes, diferente da estrutura
herdada de DKR em `include/structs.h`. Os diretórios, blocos, lotes, índices
e caixas geométricas são verificados antes de produzir os recursos PC.

| Recurso | Resultado |
| --- | --- |
| Geometria expandida | 99.206 bytes, 13 blocos |
| Dados armazenados | 3.539 vértices, 2.342 triângulos, 352 lotes |
| Cenário visível | 2.018 triângulos, 44 texturas, 264 desenhos |
| Geometria oculta | 324 triângulos excluídos pelo bit de lote 0x400 |
| Personagem | Juno: modelos 220 e 309, 534 triângulos, 21 ossos |
| Consulta de piso | 2.342 triângulos, incluindo geometria oculta |

`JFGWRL1` armazena vértices, texturas RGBA8 e materiais nativos. `JFGREG1`
guarda metadados e triângulos para consultas verticais. O catálogo do
renderer usa recursos imutáveis separados para personagem e terreno.
A câmera usa matrizes próprias em perspectiva, com profundidade D3D no
intervalo [0,1]. Terreno repete UVs; transparência é ordenada por centro de
lote, sem afirmar equivalência com os efeitos originais.

O ponto usado é o primeiro `setuppoint` inspecionado da lista 424:
(40,19,841). O objeto 126 resolve para `playerBoy`, definição 1, modelo 220,
com escala 0,26. Essa seleção é limitada ao perfil inspecionado; a política
geral de entradas, grupos e orientação ainda não foi portada.

O piso sob esse ponto está em Y=-2. A sessão preserva Y=19 como posição de
origem e aplica um deslocamento visual de aproximadamente -21,22158 ao
modelo para alinhar os pés da pose inicial. É uma consulta geométrica
nativa, sem gravidade ou colisão original implementada. Juno permanece
parado no clipe de repouso 1019, enquanto a câmera percorre um arco
controlado. A reprodução continua usando 15 quadros de origem por segundo.

## Verificação

- Linux com ASAN/UBSAN e Windows passaram nos testes de animação,
  controlador, seleção original e sessão, além de seis casos de
  geometria/câmera e 11 rejeições no perfil real.
- A sequência tem 180 frames distintos. Capturas independentes do fundo,
  terreno e personagem conferem a composição: Juno contribui com 4.553
  pixels no primeiro frame, sem mudar pixels fora de sua caixa.
- Pose neutra, ciclo, transições, deslocamento de diagnóstico, seletores
  originais e sessão de duas cenas permanecem idênticos byte a byte.
- As imagens inicial e intermediária foram inspecionadas: Juno completo,
  texturas do caminho e árvores visíveis, com perspectiva coerente.
- `make VERSION=us COLOR=0` e `cmp` passaram; ROM de saída com SHA-1
  `493ced9008dbe932d6e91179b68e8630cf23a023`.

Hashes, testes e limites estão em [region-validation.json](region-validation.json).
Os 30 fps são a cadência do vídeo de diagnóstico, não um benchmark.

## Reprodução

Com a ROM US local e o toolchain Windows já configurado:

```sh
python3 port/native/prepare_assets.py --region --out build/port-native/region
python3 port/native/build.py
python3 port/native/package.py --out build/port-native/region
```

Extrair o pacote privado em uma pasta nova no Windows autorizado e executar
por console, usando diretório de saída novo:

```sh
python run_windows.py --package CAMINHO_DO_PACOTE --out CAMINHO_DA_SAIDA
```

Recuperar `result.json` como `build/port-native/region/rtx-result.json`, além
de `frame.rgba`, `frame.background.rgba`, `frame.terrain.rgba`,
`frame.character.rgba`, `neutral.rgba`, `cycle.rgba`, `cycle-wide.rgba`,
`transitions.rgba`, `character.rgba`, `juno.rgba` e `integration.rgba`.
Então executar:

```sh
python3 port/native/check_region_frames.py
```

O vídeo privado fica em `build/port-native/region/forest-first.mp4`.
ROM, pacotes, texturas, meshes, imagens e vídeo permanecem em caminhos
ignorados pelo Git. O repositório recebe fontes, documentação e métricas.

## Limites e próximo marco

Céu original, efeitos, comportamentos dos objetos, inimigos, armas, áudio,
entrada física e ciclo completo de `levelInit`/gameplay continuam pendentes.
A consulta atual só encontra uma altura sob X/Z; não resolve paredes,
movimento varrido, rampas transitáveis ou quedas do personagem.

O próximo marco será movimento e colisão nessa mesma região, aproveitando
os contratos de cena, entidade, recursos e apresentação já integrados.
