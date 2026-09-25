# Primeira prova gráfica nativa para Windows

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
não se afirma equivalência de pixels com o N64. A câmera e a pose são
controladas, sem animação, áudio, input ou ciclo do jogo.

## Build e teste

O conversor usa apenas a biblioteca padrão do Python. O build usa o
LLVM-MinGW já instalado localmente e as bibliotecas Direct3D do Windows.

```sh
python3 port/native/prepare_assets.py
python3 port/native/build.py
python3 port/native/package.py
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

Recupere `result.json` como `build/port-native/rtx-result.json` e
`frame.rgba` no mesmo diretório, depois execute:

```sh
python3 port/native/check_frame.py
```

A inspeção dos imports do executável mostrou somente bibliotecas Windows:
D3D11, D3DCompiler, DXGI, Kernel32 e Universal CRT. O comando de compilação
inclui apenas `render.cpp` e essas bibliotecas; nenhuma dependência de
emulador é compilada estaticamente. As evidências estão em
[validation.json](validation.json).

## Continuidade

O port completo ainda exige integrar o código de jogo recompilado/adaptado,
animações, materiais especiais, iluminação, áudio, controles, salvamento
e a inicialização. A dependência do CIC será retirada no caminho do port
após mapear os efeitos da chamada, sem implementar um chip virtual.
O código matching da ROM foi preservado.

A integração RT64 foi removida da árvore ativa, preservada somente no
histórico Git (`b1753bb`). Os pontos de execução dos antigos oráculos MIPS
foram retirados/desabilitados; seus relatórios são históricos, não testes
da nova rota. Nenhuma emulação foi executada para validar este pacote.

Implementação assistida por OpenAI Codex, pelo agente principal, sem novos
subagentes. O renderer e seus shaders são código próprio deste projeto.
