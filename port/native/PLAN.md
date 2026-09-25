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
