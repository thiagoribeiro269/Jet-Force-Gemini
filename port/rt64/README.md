# Painel e personagem composto no RT64 / Windows x64

A prova gráfica produziu framebuffers reais de 320 × 240 pelo RT64, em
D3D12 na NVIDIA RTX 5070 Ti. Foram conferidos o painel/porta `swdoor`
(modelo US 35) e o Juno (nome interno `Boy`, modelo US 220), montado em pose
neutra a partir dos 21 ossos originais, com a mão `JunoHand` (modelo 309)
desenhada no ponto de encaixe do próprio jogo.

É um diagnóstico de uma imagem por execução, com câmera, cores de material
e fator de neblina controlados. A imagem vem do readback da GPU, sem janela,
swapchain, captura de tela ou filtro VI. O bootstrap do jogo continua parado
antes de `explosionFlushBlasts`; ainda faltam animação integrada, iluminação
original, apresentação contínua, áudio e gameplay. Não é um frame do boot.

O [plano](PLAN.md) registra o escopo, inclusive a correção da primeira
tentativa do personagem. Os [resultados estruturados](validation.json)
contêm métricas e hashes, sem ROM, geometria, texturas, RAM ou imagens.

## Evidências

| Teste | Resultado na GPU |
| --- | --- |
| Modelo 35 | Dois triângulos, 26.820 pixels coloridos, 7.976 cores; 520 amostras de textura com erro médio aproximado 3,41/255 |
| Corpo 220 + mão 309 | 534 triângulos submetidos, 18 cargas de textura, 9.212 pixels coloridos e 7.213 cores; mão e visor presentes em inspeção visual |
| Regressão do modelo 35 | O executável final do personagem produziu exatamente os mesmos bytes RGBA do primeiro painel aprovado |

A comparação de textura do painel aproxima o filtro de três pontos por
bilinear. Para o personagem, o verificador confere limites da geometria,
cobertura e oito posições transformadas do RT64; o maior erro observado
nessas posições foi `0.00001357`. Nenhum teste afirma igualdade de pixels
com o RDP físico. O relatório bruto do renderer deixa a imagem como
`candidate_frame_unreviewed`; `check_frame.py` ou `check_character.py`
produz o relatório de validação separado.

O PNG de apresentação é RGB opaco, preservando os canais RGB do readback.
O alfa do render target do RT64 também carrega cobertura do RDP; no Juno,
quase todos os pixels desenhados vieram com alfa `7/255`. A primeira
exportação RGBA ficou quase transparente no WhatsApp. Após o relato de
Thiago, a exportação foi corrigida e reenviada; a validação conferiu os
76.800 pixels RGB, o tipo de PNG sem alfa e os CRCs. Os bytes RGBA brutos
permanecem preservados para diagnóstico. A porta tinha alfa 255 em toda
a imagem e não sofreu esse problema de exibição.

A carga original do personagem passou em comparação com MIPS: 44
transferências ROM, 14 GPRs de retorno/preservação e RAM, exceto as quatro
regiões diagnósticas já justificadas em [graphics](../graphics/README.md).
Todos os vértices, triângulos e payloads das 18 entradas de textura foram
comparados com a descompactação independente da ROM. O modelo armazena
520 triângulos; a lista gerada em `model+0x74` submete 502. O adaptador
segue essa lista, sem inventar os 18 restantes.

## Correções após a inspeção do usuário

Thiago identificou a falta de uma mão e de parte do capacete. A mão não
pertence ao mesh principal: `JunoHand` carrega 43 vértices e 33 triângulos,
dos quais 32 são emitidos pela lista original. Reutiliza uma textura do
corpo. A carga adicional passou na comparação MIPS de memória, registradores
e três transferências ROM; o diagnóstico composto encerra cinco workers.
O primeiro registro de attachment do modelo aponta para o osso 6. A matriz
selecionada foi conferida byte a byte executando `objMakeGunMtx` MIPS com
um objeto Juno e um item de mão controlados. Não há encaixe estimado pela imagem.

O capacete revelou um erro na câmera do diagnóstico: o sinal da profundidade
discordava da orientação das faces. Uma execução sem culling mostrava as
costas, enquanto a imagem com culling mostrava a frente através delas.
Foi corrigido o sinal de Z da projeção ortográfica, preservando os flags
originais das faces. A diferença RGB entre as duas execuções diagnósticas
caiu de 8.937 para nove pixels; o visor passou a aparecer corretamente.
Não se habilitou desenho de faces duplas para esconder o defeito.

Os 18 triângulos que o corpo não submete estão em lotes marcados com `0x400`,
ignorados explicitamente por `makeModelGfx`; permanecem omitidos. As imagens
anteriores são marcos diagnósticos, não referências visuais completas.

## Ossos e pose neutra

A instância recém-carregada contém matrizes identidade. Os vértices do
personagem pertencem aos espaços locais dos ossos, então desenhá-los
diretamente deixa as partes sobrepostas. `prepare_character.py` soma as
translações da hierarquia original com arredondamento float32, mantendo
rotações nulas. Compara os 1.344 bytes das 21 matrizes com a execução MIPS
de `gen_anim_data`, usando descritor neutro e transformação global identidade.

O snapshot original fica em `load-ram.bin`. O snapshot de renderização
recebe somente a substituição documentada dessas matrizes; o verificador
confere que os demais bytes são idênticos. Essa preparação é um fixture do
teste, não a integração nativa do sistema de animação. O adaptador C++ lê
as translações e fornece ao RT64 o transform de cada canto, preservando os
vértices locais e os UVs originais. Rotação/escala de ossos ainda é rejeitada.

## Adaptador e dependências

RT64 está fixado em `43373749dac9bbc1b653e6a02aed40a9e1783bed`. Ele não
reconhece F3DJFG nesse snapshot. O harness configura as constantes F3D do
RT64 e usa explicitamente nosso adaptador limitado, sem simular identificação
de microcódigo. O caminho do painel permanece separado do personagem.

O adaptador confere endereços, contagens, alinhamento, cache de vértices,
seleção de matrizes, flags de face e formatos de textura. Encaminha os
comandos RDP originais, em ordem, aos handlers do RT64; converte os vértices
JFG de dez bytes para entradas canônicas e duplica os UVs por canto. O
personagem acrescenta DMA de matrizes, MOVEWORD, vários lotes e RGBA16,
IA8 e RGBA32. Billboard, outros comandos e poses fora do contrato falham.

A codificação foi examinada em `makeModelGfx`, `objPrintModelObject`,
`func_8003BE68`, `src/hasm/gen_anim_data.s` e RAM real. Os significados dos
campos de DMA e da flag de face também foram confrontados com os handlers
[F3DJFG do GLideN64](https://github.com/gonetz/GLideN64/blob/master/src/uCodes/F3DDKR.cpp)
e a rotina [gSPDMATriangles](https://github.com/gonetz/GLideN64/blob/master/src/gSP.cpp).
Não foi incorporado código desses handlers GPL.

`build_rt64.py` prepara uma cópia de build ignorada pelo Git e aplica os
ajustes de cross-compilation de forma reproduzível. Os checkouts originais
de RT64/Plume permanecem limpos. Além dos ajustes de toolchain, a cópia do
Plume trata o destino de readback como buffer: evita desreferenciar o campo
de textura nulo ao configurar sample positions para `copyTextureRegion`.
As licenças MIT de RT64 e Plume acompanham as fontes deste harness.

Os ubershaders originais do RT64 foram usados. A especialização DXIL está
desabilitada neste harness porque os compiladores Linux e Windows, mesmo
no commit DXC 1.8 correspondente, gravam strings de versão diferentes e o
linker rejeita as bibliotecas. Não há alteração de metadados DXIL. Corrigir
esse build para a mesma versão efetiva permanece como pendência. O pacote
Windows usa DLLs oficiais preparadas por `prepare_dxc.py`, com hashes e licenças.

## Reprodução privada

Requer a ROM US local, o ELF matching e o perfil de CPU preparado por
`port/graphics/run.py`. Os resultados do diagnóstico ficam em `build/`.

```sh
python3 port/rt64/prepare_dxc.py
python3 port/rt64/build_rt64.py
python3 port/rt64/prepare_frame.py
build/port-recomp/.venv/bin/python port/rt64/prepare_character.py \
  --with-hand --out build/port-character/complete
python3 port/rt64/package_windows.py --out build/port-character/complete
```

**O ZIP gerado contém RAM e assets do jogo; é privado e não pode entrar
em releases ou commits.** Depois de transferi-lo e extrair em uma pasta
própria no Windows autorizado:

```powershell
python -I -O package/run_windows.py --package package --out result
```

O runner confere hashes, executa primeiro o teste de decoder e depois a
GPU. Os limites são de 30 e 180 segundos, respectivamente; só encerra um
filho criado por ele se esse filho exceder seu prazo. Nenhum processo ou
serviço preexistente é encerrado. O pool de texturas substitutas tem limite
de 64 MiB, que **não** representa um limite da VRAM total do RT64.

Recupere `result.json` como `rtx-result.json` e `frame.rgba` para o diretório
privado correspondente e valide:

```sh
python3 port/rt64/check_character.py --out build/port-character/complete
# Para o painel, usar seu pacote e os arquivos do seu próprio teste:
python3 port/rt64/check_frame.py --out build/port-rt64/regression-model35
```

`decoder_test.cpp` aceita RAM/lista/base de vértices e, para o personagem,
uma base adicional de matrizes float. Para a mão rígida, acrescenta-se
`hand` após a matriz. Os três casos válidos e 31 corrupções (14 do painel,
11 do corpo, seis da mão) passaram no Linux com ASAN/UBSAN e no Windows.
A ROM matching permaneceu byte a byte idêntica, SHA-1
`493ced9008dbe932d6e91179b68e8630cf23a023`.

Trabalho assistido por OpenAI Codex. As delegações iniciais terminaram;
a extensão do personagem e suas verificações foram feitas pelo agente
principal, sem novos subagentes, conforme solicitado por Thiago.
