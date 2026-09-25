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

## Terceiro pacote: seleção e transição de clipes

Plano fechado antes da implementação, a partir de `73aa7e3`. Acrescentar
o clipe 1030 (índice 14 do Juno), com dez quadros, stride de dez bytes e
77 bits por quadro. A inspeção do formato confirmou loop, 21 canais de
esqueleto e ausência de escala animada, como no clipe 1026 já validado.

1. Evoluir a cena para `JFGNAT3`, guardando os dois clipes e preservando
   leitura de `JFGNAT1/2`. Continuar convertendo dados da ROM diretamente.
2. Criar um controlador C++ com seleção por ID, relógio em segundos e
   transição angular/local. A origem será uma captura da pose visível;
   o destino continuará avançando. Uma nova seleção durante a transição
   capturará a pose misturada, evitando salto de pose. Pedidos repetidos
   para o mesmo ID não deverão reiniciar o clipe nem a transição.
3. Validar tempo/IDs antes de alterar estado. Usar testes matemáticos de
   início/fim, interrupção, repetição e passos de tempo equivalentes.
   Os resultados não serão apresentados como o algoritmo de blending
   original do N64 ou como transição de gameplay já integrada.
4. Alimentar comandos explícitos por arquivo de diagnóstico, sem teclado
   ou controle físico: B em frame 32, repetição em 34, A em 36 (interrupção),
   B em 56 e A em 80. Renderizar 96 frames a 30 fps, com durações controladas
   de 0,20 a 0,30 segundo. Conferir ausência de salto no instante do comando.
5. Revalidar o ciclo anterior e a pose neutra, executar na RTX por console
   SSH e produzir vídeo privado da sequência. Publicar somente código,
   documentação e métricas, mantendo o trabalho solo e sem emulação.

Este bloco prepara a API para futuras entradas do jogo. Não implementa
locomoção, colisão, câmera jogável, áudio, eventos, IK ou controle físico.

A primeira validação visual detectou pés cortados durante a mistura, embora
as poses finais coubessem na câmera. A sequência recebe um enquadramento
fixo mais amplo; as regressões anteriores continuam usando a câmera original.
Um ciclo isolado com a câmera ampla permite comparar os frames da sequência
antes dos comandos e depois do fim das transições. Isso não resolve nem
promete contato físico dos pés com o chão: foot planting/IK segue fora do escopo.

Resultado: dois clipes convertidos, controlador C++ validado e sequência
de 96 frames concluída na RTX. Quatro seleções efetivas, uma repetição
ignorada, interrupção sem salto instantâneo de matrizes e 72 imagens
distintas. Pose neutra e ciclo anterior byte a byte preservados. A câmera
ampla eliminou o corte no teste; vídeo e traços continuam privados.

## Quarto pacote: estado e deslocamento do personagem

Plano fechado a partir de `25c6ffe`, antes da implementação. A leitura
estática de `objAnimSetMove` e do seletor do overlay 16 em `0x4F78`
confirmou que o jogo usa índices locais e remapeamentos dependentes do
estado. Ainda não identificamos todos os significados desses estados;
este pacote cria uma política explícita do port, sem atribuí-la ao original.

1. Inspecionar a pose constante do clipe 1071, índice 51 do Juno: três
   quadros, stride zero e ângulos constantes, sem escala animada. Acrescentar
   esse perfil ao conversor limitado e ao formato existente `JFGNAT3`.
   Usá-lo como repouso provisório somente se a inspeção visual for adequada.
2. Criar um controlador C++ que receba eixos X/Z e um comando de postura
   baixa, selecione repouso/movimento/postura baixa e conduza o controlador
   de animação. Movimento usa 1026; postura baixa usa 1030. Velocidade,
   zona morta, prioridade e tempos de mistura são escolhas documentadas
   deste protótipo. Não portar a física original por aproximação silenciosa.
3. Separar posição/orientação do personagem da translação local dos ossos.
   Normalizar diagonais, limitar a velocidade angular e preservar posição
   ao parar ou baixar. Conferir comandos/tempo antes de alterar o estado.
4. Alimentar entradas por um arquivo reproduzível, sem acesso a dispositivos
   físicos ou janela. Testar partida, repetição, diagonal, interrupção,
   postura baixa prioritária, parada e retomada. Registrar estado, pose,
   posição e direção em cada frame; usar câmera fixa que comporte o trajeto.
5. Validar matemática e estado com dados sintéticos no Linux e Windows,
   renderizar na RTX por SSH e conferir regressões de pose, ciclo e mistura.
   Inspecionar imagens e produzir vídeo privado. Publicar somente fontes,
   documentação e métricas; conferir também o build matching da ROM.

Colisão, gravidade, cenário, câmera jogável, teclado/controle físico, áudio,
eventos de animação e integração à lógica original permanecem pendentes.
O resultado será um personagem conduzido por comandos nativos de teste,
não o jogo já jogável nem uma reprodução da movimentação original.

Resultado: a pose 1071 foi inspecionada e adotada como repouso provisório.
O controlador executou 13 comandos e dez mudanças de estado em 180 frames
na RTX, com posição e giro nativos. Diagonais, prioridade da postura baixa,
zona morta, interrupções e dez posições analíticas passaram. Foram obtidas
149 imagens distintas; repouso estabiliza em frames idênticos. Os três
diagnósticos gráficos anteriores continuam idênticos byte a byte. O teste
de giro final foi corrigido para considerar a duração de 0,4 segundo:
72° de giro deixam 18° de orientação, que a parada deve conservar.
O relatório do renderer identifica o clipe inicial 1071 neste perfil.

## Quinto pacote: seleção original de animações

Plano fechado a partir de `5ca68f3`. A leitura estática identificou duas
rotinas pequenas do overlay 16: `0x4E08` escolhe um movimento por componentes
de movimento e campos do personagem; `0x4F78` remapeia o índice conforme
objeto na mão, `controlPlayerGunWeight` e o campo `+0x1F4`. A tabela tem
52 registros de cinco bytes em `data+0x280`. Os dois últimos bytes escolhem
um dos sete perfis enviados a `controlSetTransition`, depois do remapeamento.

1. Extrair a tabela e o limite de repouso (float em rodata+0x144) da ROM
   local, conferindo SHA-1, bytes das rotinas e realocações. Exportar dados
   privados num formato pequeno do PC, com IDs globais da tabela do modelo.
2. Portar as duas decisões para C++ tipado, sem executar instruções MIPS
   nem representar RAM do console. Conservar os nomes por offset dos campos
   cujo significado não esteja suficientemente confirmado. A escolha
   aleatória entre 16 e 19 será um argumento explícito; não inventar RNG.
3. Ligar a seleção ao `AnimationPlayer`, preservando clipe/relógio em
   repetição e propagando o perfil de transição como metadado. O parâmetro
   original de `objAnimSetMove` é a fração inicial do clipe, limitada a
   [0,1], e não o tempo de mistura. Acrescentar suporte explícito a essa
   fração; a mistura em segundos continua sendo a política nativa atual.
4. Converter os clipes necessários a uma sequência limitada de repouso,
   três faixas de movimento, retorno e variantes por arma/objeto na mão.
   O repouso original solicitado pela rotina usa índices 16–19; o 51 da
   prova anterior era uma escolha provisória, não o repouso padrão comprovado.
   Destinos sem clipe convertido devem falhar antes de alterar o estado.
5. Testar limites estritos, empates, prioridades, perfil do índice resultante,
   fração inicial e repetição, usando dados sintéticos e casos reais revisados
   estaticamente. Executar no Linux ASAN/UBSAN e no Windows; renderizar os
   destinos selecionados na RTX e revalidar as provas gráficas anteriores.
   Conferir o build matching e publicar fontes/documentação, sem assets.

O diagnóstico fornecerá estados nos mesmos pontos das APIs originais. Não
implementará ainda o ciclo inteiro de `boyControl`, física, cadência dos
pedidos, RNG, armas, colisão, áudio ou o conteúdo de `controlSetTransition`.
Perfis dessa última função não são durações equivalentes à mistura atual.
O vídeo mostrará seleção de animações no lugar, sem atribuir às componentes
originais uma conversão ainda não comprovada para unidades por segundo.

Resultado: as duas decisões foram portadas e ligadas ao player nativo.
Auditoria de 1.700 bytes de cinco rotinas e 15 realocações aprovada. Linux
ASAN/UBSAN e Windows passaram nos casos de limites, prioridades, fase,
repetição, erro sem mutação e 20 seleções da tabela real. A RTX produziu
180 frames com sete clipes, 167 imagens distintas e oito trocas sem salto
instantâneo. O início em fração 0,5 do clipe de 50 quadros resultou em fase
24,5, sem confundir com a mistura nativa de 0,2 segundo. As quatro provas
gráficas anteriores permaneceram idênticas byte a byte.

## Mudança de escala: estrutura geral do port

Thiago pediu priorizar o contexto global e absorver os detalhes dentro
dele. Essa orientação substitui a cadência isolada de animações como próxima
prioridade. O plano da integração, fechado antes da implementação a partir
de `42f9ab6`, está em [INTEGRATION_PLAN.md](INTEGRATION_PLAN.md).

O pacote seguinte, autorizado após essa integração, é a região original
`Forest First` no mesmo host. O escopo e as evidências de formato estão em
[REGION_PLAN.md](REGION_PLAN.md), definidos antes de implementar o conversor.

Resultado: Forest First e Juno integrados e validados na RTX, com catálogo
de dois recursos, perspectiva e consulta vertical de piso. As seis provas
anteriores permanecem idênticas. O checkpoint está em [REGION.md](REGION.md);
o próximo marco é movimento e colisão dentro da região.
