# Port nativo sem emulação — decisão de 25/09/2026

Thiago determinou eliminar o RT64 e toda emulação, além de não copiar código
de emuladores. Esta decisão substitui a rota gráfica anterior e a proposta
de reproduzir CIC/PIF. O último checkpoint dessa rota é `b1753bb`, mantido
no histórico do Git. Os arquivos externos do CIC nunca foram integrados.

## Arquitetura escolhida

- CPU: recompilação estática e adaptação das rotinas necessárias para o PC,
  com execução nativa; nenhuma CPU MIPS interpretada durante execução/testes.
- Assets: ler a ROM local, descompactar e converter os formatos do jogo em
  meshes, texturas e esqueletos normais do PC. Não executar display lists,
  microcódigo RSP ou comandos RDP.
- Gráficos: renderizador próprio usando Direct3D do Windows. A primeira
  prova usará D3D11, adequado ao framebuffer offscreen e à RTX, com shaders
  HLSL próprios e amostragem nativa. A aparência pode diferir do rasterizador
  N64; não prometer equivalência de pixels.
- CIC: retirar a dependência do cartucho apenas no caminho do port, depois
  de mapear consumidores e efeitos da chamada. Não simular resposta do chip.
- Testes: invariantes dos dados, conservação da geometria, validação do
  compilador, execução nativa e readback da GPU. Unicorn deixa de ser usado.

## Primeiro pacote verificável

1. Retirar a integração RT64 ativa e desabilitar a referência MIPS. Preservar
   a evidência histórica no Git e os arquivos privados sem publicá-los.
2. Preparar conversão direta dos modelos 220/309, respectivas texturas e
   hierarquia de ossos. Conferir índices, limites e origem dos dados sem
   importar os antigos runners de emulação.
3. Desenhar o personagem composto em uma textura D3D11 por triângulos e
   recursos nativos, usando câmera controlada e shaders próprios. Produzir
   uma imagem RGB opaca após a sincronização do dispositivo.
4. Executar por console SSH no Windows RTX, sem janela ou interação com a
   área de trabalho, preservando processos existentes. Publicar somente o
   código e a documentação; ROM, assets e imagens continuam locais.

Esta reconstrução da camada gráfica é trabalho adicional. Animações,
materiais especiais, iluminação, áudio, controles e integração ao ciclo do
jogo permanecem etapas posteriores. Os resultados da rota abandonada não
comprovam automaticamente o funcionamento deste novo caminho.

## Segundo pacote: animação nativa do Juno

Planejado a partir de `fe54045`, antes de alterar o formato de cena ou o
renderer. A leitura estática do carregador e de `src/hasm/gen_anim_data.s`
identificou as tabelas 0x28/0x29 (associação modelo/clipes), 0x2A/0x2B
(offsets/dados compactados por bits) e 0x2C/0x2D (mapeamento de canais para
ossos). Juno tem 52 entradas. A primeira aponta para o clipe 1026, com 16
quadros, 21 entradas de esqueleto, stride de 25 bytes e 193 bits por quadro.

1. Converter esse clipe diretamente para quadros de translação raiz e
   ângulos por osso. Validar consumo de bits, limites, remapeamento e loop.
   O conversor usa somente Python padrão e dados da ROM local.
2. Evoluir a cena para guardar vértices locais, vínculo a ossos e o clipe.
   A interpolação, matrizes/hierarquia e transformação da malha serão
   executadas em C++ no PC a cada quadro, com matemática própria e testável.
3. Usar interpolação pelo caminho angular curto, composição X/Y/Z e vínculo
   da mão ao osso 6. A equivalência numérica bit a bit com o código MIPS não
   será afirmada. Nenhum emulador será usado para conferir o resultado.
4. Renderizar um ciclo limitado de 33 quadros: 32 amostras e retorno ao
   quadro inicial, com avanço de meio quadro de origem por amostra.
   A taxa de apresentação será 30 fps (15 quadros de origem por segundo),
   definida para o diagnóstico, não identificada como velocidade original.
5. Conferir a pose neutra anterior, testes matemáticos de hierarquia e
   interpolação, variação real dos frames na RTX e fechamento do loop.
   Produzir um vídeo privado e registrar as limitações e o próximo passo.

Não entram neste pacote blending entre clipes, IK/mira, eventos de animação,
movimento/colisão de gameplay ou a inicialização completa do jogo. O clipe
selecionado será identificado pelo ID; seu significado só será nomeado
após evidência visual/dados suficientes.

Resultado deste pacote: o clipe 1026 foi convertido diretamente e executado
em C++ na RTX. As 32 amostras do ciclo são distintas; o frame 33 fecha com
bytes idênticos ao primeiro. A pose neutra também permaneceu idêntica à
referência nativa anterior. Inspeção de quatro fases confirmou articulação
do corpo e da mão. Vídeo privado de três ciclos gerado, com velocidade
controlada e sem reivindicar comportamento completo de gameplay.
