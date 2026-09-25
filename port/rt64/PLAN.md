# M2.2: framebuffer controlado no RT64

Alvo mantido por Thiago em 25/09/2026: Windows x64 com NVIDIA. Android fica
fora do escopo por enquanto. Este experimento sucede `1963f15` e usa o mesmo
modelo 35 e a textura `0x9097`, já conferidos contra ROM e execução MIPS.

## Rota escolhida antes da implementação

Usar o RSP/RDP em alto nível e os shaders de rasterização do RT64 fixado em
`43373749dac9bbc1b653e6a02aed40a9e1783bed`, com um adaptador explícito para o
subconjunto F3DJFG presente na lista real. Renderizar uma única workload em
uma textura RGBA8 de 320×240, sem HDR/MSAA, janela, swapchain ou filtro VI.
Ler o framebuffer por buffer de readback após a sincronização da GPU.

A inspeção de `objPrintModelObject` encontrou o comando `0xBF` que fornece
os offsets DMA. A instância carregada pelo diagnóstico tem a base dos
vértices em `instance+4`, mas não tem as matrizes/câmera do jogo preparadas.
Este teste fornece uma projeção ortográfica controlada. Não a apresentará
como câmera original, nem como primeiro frame do boot completo.

## Pacotes independentes

1. **Build:** compilar RT64 e suas dependências fixadas como biblioteca
   estática Windows. Usar DXC e `file_to_c` executáveis no host da compilação.
   Alterações necessárias ao build ficam em patches reproduzíveis sobre a
   cópia privada; os vendors herdados e o upstream permanecem preservados.
2. **Adaptador:** conferir/decodificar `0x04`, `0x05` e `0x07` do caso real,
   converter vértices de 10 bytes para a entrada canônica do RT64, respeitar
   UV por canto e encaminhar os comandos RDP de textura/tile aos handlers
   existentes. Rejeitar comandos e estados fora do subconjunto declarado.
3. **Harness:** inicializar D3D12, shaders, caches e uma fila de renderização
   sem `Application::setup` e sem `updateScreen`. Limitar threads e recursos,
   exigir dispositivo NVIDIA e registrar a identificação. Uma fila de
   apresentação inerte atende somente às referências internas desse teste.
4. **Verificação:** conferir contagens, índices, UV, estado e submissão;
   executar na RTX por console SSH; capturar pixels em arquivo do projeto
   após fence/wait. Conferir dimensões, pixels não uniformes e correspondência
   com a geometria/textura escolhidas. Incluir casos de rejeição relevantes
   para limites e comandos não suportados.

## Critério de conclusão e limites

O pacote só comprova renderização após executar a GPU real e ler os pixels
produzidos pelo RT64. Compilar, decodificar a lista ou gravar uma prévia de
texels não satisfaz esse critério. Uma dificuldade de build deve ser
registrada com sua causa concreta; não será contornada substituindo o RT64
por um rasterizador próprio ou declarando sucesso sem imagem.

O resultado esperado é um quadro de diagnóstico com dois triângulos
texturizados. Iluminação completa, billboard, matrizes F3DJFG gerais,
temporização RSP/RDP, filtro VI, apresentação, áudio e gameplay ficam fora
desta prova. Os assets, shaders compilados, binários e pixels ficam em
`build/`, ignorado pelo Git. Nenhuma captura de tela pessoal é necessária.

## Ajuste identificado na execução

O pacote DXC do RT64 fixado traz compilador Linux 1.8 e runtime Windows 1.7.
Uma DLL Windows oficial do mesmo commit 1.8 foi preparada com hashes
verificados, mas o linker DXIL também exige igualdade da string de versão:
`1.8.0.4461` no build Linux e `1.8.2403.37` no Windows oficial. As duas
tentativas falharam antes de produzir uma imagem.

A prova fica, portanto, no caminho de **ubershaders stock do RT64**. A
compilação especializada opcional é parada e juntada dentro do cache criado
pelo próprio harness, antes de haver trabalho enfileirado. Os shaders reais
de rasterização e a GPU continuam sendo usados. Essa configuração é
restrita a uma workload; não modifica metadados DXIL nem força o linker a
aceitar versões incompatíveis. Alinhar também as strings de versão do DXC
permanece como pendência para habilitar a especialização.

Planejamento baseado nas fontes fixadas de RT64/Plume, no ASM do chamador e
na RAM já validada. As delegações iniciais terminaram. Por solicitação de
Thiago, os próximos testes e a integração são realizados apenas pelo agente
principal, sem novas delegações.

## Segundo asset: personagem (planejado antes da extensão)

Pedido de Thiago: testar um personagem. O modelo US 220 tem nome interno
`Boy`, 660 vértices, 520 triângulos armazenados e 18 entradas de textura.
O carregador nativo existente já o carregou. A instância aponta para os
vértices originais e contém 21 matrizes float inicializadas como identidade
por `func_8003BE68`. A primeira imagem demonstrou que essa inicialização
ainda não monta o corpo: os vértices estão no espaço local de cada osso.

1. Reproduzir a carga e comparar a chamada com MIPS e os dados imutáveis com
   a ROM. Exportar somente para `build/port-character`.
2. Acrescentar um caminho explícito de pose inicial ao adaptador: vários
   lotes de vértices/triângulos, texturas RGBA16/IA8/RGBA32 e seleção de
   matrizes. Preparar uma pose neutra somando as translações na hierarquia
   original dos 21 ossos, com arredondamento float32, e comparar as matrizes
   com `gen_anim_data` MIPS usando rotações nulas e transformação global
   identidade. Aceitar somente essas matrizes de translação; rejeitar poses
   com rotação/escala, billboard ou comandos desconhecidos. Não interpretar floats
   da instância como matrizes fixas prontas para o RSP.
3. Encaminhar o estado RDP original, manter UV por canto e flags de face.
   Usar câmera ortográfica controlada e o mesmo framebuffer D3D12 da prova
   anterior, com regressão separada da porta/painel.
4. Executar por console SSH no Windows RTX, recuperar os pixels produzidos
   pela GPU, conferir contagens/cobertura e inspecionar visualmente o
   personagem. Pose animada, iluminação original e cena de gameplay ficam
   como trabalho posterior.

O gerador da pose é uma preparação explícita do diagnóstico, conferida
contra o MIPS; ainda não representa a integração nativa da animação do jogo.
O snapshot original da carga fica separado do snapshot usado pelo renderer,
que recebe somente a substituição documentada das 21 matrizes. A câmera
também corrige o eixo vertical para a convenção de viewport do RT64.

## Continuação: mão e capacete (relato de Thiago)

Após a correção do PNG, Thiago observou a falta de uma mão e de parte do
capacete. A prova anterior comprova pixels, carga e pose no escopo declarado;
não comprova que o personagem já contém todas as peças exibidas pelo jogo.

Antes de ampliar a implementação:

1. Conferir os lotes omitidos pelo próprio `makeModelGfx`. A inspeção inicial
   encontrou 18 triângulos em lotes com flag `0x400`, que a rotina ignora.
   Não forçar esses lotes para preencher a imagem.
2. Conferir o modelo separado US 309 `JunoHand`: a carga nativa já produziu
   uma lista com 32 triângulos visíveis, sem esqueleto próprio. Examinar o
   vínculo do chamador com o osso do corpo antes de desenhá-lo junto.
3. Isolar o relato do capacete com comparação diagnóstica de culling, mantendo
   claramente separados os dados originais e os dados alterados somente
   para o diagnóstico. Não promover uma imagem com faces forçadas como
   correção de fidelidade sem identificar o estado correspondente do jogo.
4. Integrar somente o caso comprovado, repetir o teste na RTX e registrar
   limites remanescentes. Animação continua como etapa posterior.

Resultado: mão integrada pelo osso 6 indicado no registro original de
attachment, com carga e `objMakeGunMtx` conferidos em MIPS. A projeção do
diagnóstico recebeu Z negativo para compatibilizar profundidade e faces;
a comparação com culling desligado passou de 8.937 para nove pixels RGB
diferentes. Cena composta aprovada com 534 triângulos e 18 cargas de textura.
Os lotes originais omitidos continuam omitidos. Mais detalhes e evidências
em `README.md` e `validation.json`.
