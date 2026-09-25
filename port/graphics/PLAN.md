# Experimento gráfico com um asset real

Este é o primeiro pacote do marco 2 do [roteiro](../ROADMAP.md), após
`ddac7ad`. O objetivo do marco continua sendo uma imagem controlada; este
pacote fecha primeiro o contrato e o carregamento dos dados que a produzirão.

## Decisões tomadas antes do teste

O RT64 foi examinado no commit
`43373749dac9bbc1b653e6a02aed40a9e1783bed`. Seu registro não contém handlers
F3DJFG/F3DDKR. O caminho HLE identifica texto/dados por hashes e rejeita
microcódigos não reconhecidos. Adicionar somente hashes ou selecionar F3D
genérico não implementaria os comandos específicos do JFG.

O experimento usará **modelo 35**, cujo arquivo contém o nome `swdoor`, e a
**textura `0x9097`** referenciada por ele. A escolha é estrutural: quatro
vértices, dois triângulos e uma textura RGBA16 de 32×64 pixels. A identidade
visual ou a função desse objeto numa fase não é pressuposta.

## Procedimento e critérios

1. Conferir os intervalos do microcódigo contra ROM/ELF e testar o algoritmo
   de identificação da versão fixada do RT64. Distinguir identificação
   suportada, comandos implementados e renderização efetivamente testada.
2. Criar um perfil isolado que preserve o bootstrap já validado e acrescente
   `modLoadModel`, `texLoadTexture`, `makeModelGfx` e suas dependências reais.
   A análise inicial fechou a árvore sem novo import de plataforma.
3. Executar `modLoadModel(35, 0)` num worker do diagnóstico. Conferir a
   instância devolvida, os caches, a textura associada, ponteiros, geometria
   e comandos na RAM. Comparar com descompressão independente da ROM e com
   o mesmo caminho MIPS. Encerrar todos os workers da sessão.
4. Exportar apenas para caminhos privados os pixels, geometria e listas
   produzidas, permitindo inspecionar o resultado sem publicar assets.
   Uma prévia de pixels decodificados, se produzida, será identificada como
   diagnóstico de asset, não como imagem renderizada pelo RT64.
5. Executar os testes nativos no Windows x64 por console SSH e registrar o
   contrato que falta para o renderizador. Manter explícitos os comandos
   ainda sem adaptação.

## Condição para avançar à imagem

Os dados e a lista real precisam passar na comparação. Depois, selecionar
uma rota comprovável: implementar handlers próprios F3DJFG sobre o RT64 ou
executar o microcódigo RSP original e fornecer seus comandos RDP ao renderer.
A segunda rota também exige experimento: o texto ocupa `0x1290` bytes,
maior que IMEM, e seu carregamento dinâmico ainda precisa ser mapeado. O
RSPRecomp fixado possui `overlay_slots`/`do_overlay_swap`; a documentação
resumida sobre ausência de overlays não basta para descartá-lo.

O RT64 `Application` examinado cria janela e swapchain; uma via de captura
sem janela não está pronta nesse fluxo. O pacote atual usa somente console,
sem abrir interfaces ou capturar telas da RTX. O marco 2 só será marcado
concluído após o experimento de imagem, com a rota e seus limites registrados.

Fontes: microcódigo e ASM locais conferidos com a ROM US;
[RT64 GBI](https://github.com/rt64/rt64/blob/43373749dac9bbc1b653e6a02aed40a9e1783bed/src/gbi/rt64_gbi.h),
[identificação](https://github.com/rt64/rt64/blob/43373749dac9bbc1b653e6a02aed40a9e1783bed/src/gbi/rt64_gbi.cpp)
e [Application](https://github.com/rt64/rt64/blob/43373749dac9bbc1b653e6a02aed40a9e1783bed/src/hle/rt64_application.cpp).
