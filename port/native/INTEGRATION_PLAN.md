# Estrutura geral do port — plano de 25/09/2026

O objetivo passa a ser integrar o port a partir de seu fluxo completo.
Os recortes já validados continuam úteis como regressões. Não basta somar
animações ou funções isoladas para demonstrar que uma partida funciona.

## Contexto original já identificado

`mainThread` inicia o jogo, solicita uma mudança de nível e entra no ciclo
`func_80044938`. Este chama a atualização de jogo em `func_800457F4`, que
alcança `objObjectsTick`; a lista de jogadores passa por `controlPlayer`
e despacha `boyControl` no overlay 16. A aplicação também possui caminho
de troca de nível, câmera, efeitos, áudio, apresentação e liberação diferida.

A descoberta global deve resolver chamadas entre overlays e chamadas do
programa principal via `TrapDanglingJump`. A tabela principal começa com
um contador de quatro bytes, seguido por registros de oito bytes
`(índice ORT, offset/flags)`. Comentários antigos que sugerem pares simples
de endereço/índice não bastam para interpretá-la. `runlinkInitialise` e
`src/hasm/ido/trapDanglingJump.s` são as referências do formato.

## Entrega planejada

1. Produzir inventário estático do programa inteiro a partir do ELF matching
   e da ROM US: funções, módulos, chamadas diretas, destinos de realocação,
   entradas sem tamanho e saltos indiretos não resolvidos. Conferir bytes;
   não executar CPU nem importar antigos runners. Publicar síntese legível
   e ferramenta reproduzível, sem ROM ou instruções extraídas.
2. Reescrever o roteiro vigente por subsistemas e marcos de integração,
   preservando o roteiro anterior como histórico. Distinguir explicitamente
   código nativo ativo, provas antigas que precisam de revisão e funções
   ainda não integradas. Não usar contagem de funções como percentual do port.
3. Separar recursos/cena, simulação e backend D3D11. O renderer deve receber
   poses e transformações prontas por entidade; não escolher animações ou
   controlar o ciclo do jogo. O carregamento de assets deve funcionar sem
   depender dos headers do Windows.
4. Implementar uma sessão nativa com inicialização, carga de cena, entidades,
   atualização em passos fixos, pausa/retomada, troca de cena validada e
   encerramento. Criar snapshots para apresentação independente da taxa de
   atualização. Comandos são fornecidos pelo teste, sem janela nem dispositivo.
5. Integrar os recursos reais do Juno, o movimento de diagnóstico existente
   e o seletor recuperado do jogo nessa sessão. Preservar a separação entre
   movimento provisório e campos originais de animação. Exercitar múltiplas
   entidades, pausa e recarga; não apresentar a cena de integração como
   uma fase original já carregada.
6. Validar ciclo/recursos/erros no Linux e Windows, comparar evolução sob
   diferentes cadências de apresentação e executar a integração na RTX.
   Conferir regressões visuais anteriores e ROM matching. Publicar fontes,
   mapa, contratos e evidências; manter assets, frames e vídeos privados.

## Fronteiras que permanecem explícitas

O host é uma arquitetura nativa do PC, não uma simulação de scheduler,
VI, RSP/RDP, CIC/PIF ou memória física do console. Os perfis legados não
serão ligados automaticamente só porque já recompilavam funções.

Carregamento de regiões/cenário original, colisão, física original, combate,
IA de inimigos, áudio, salvamento, interface original e dispositivos de
entrada continuam requisitos do port completo. A infraestrutura precisa
marcar esses serviços ausentes e falhar em pedidos que dependam deles;
não criar funções vazias que declarem sucesso. O próximo marco será
escolhido pela dependência que mais aproxima uma cena original jogável.

## Resultado observado

Inventário concluído para 2.941 entradas em 156 seções de módulo, com
14.010 relações diretas e limites explícitos para destinos indiretos e
extensões desconhecidas. Os bytes de funções foram conferidos contra a ROM.
O roteiro vigente foi reescrito por subsistemas e integração.

`NativeSession`, o carregador portátil e o backend D3D11 separado foram
implementados e executados na RTX. Duas cenas, quatro criações de entidade,
até duas simultâneas, pausa/retomada, troca e remoção passaram. Foram obtidas
150 imagens distintas em 180 frames, com 31 frames idênticos durante a pausa.
As cinco provas gráficas anteriores permaneceram idênticas byte a byte.

Os testes da sessão com recursos sintéticos/reais passaram no Linux
ASAN/UBSAN e Windows, incluindo 12 pontos comuns sob apresentação a
30/60/144 Hz. O próximo marco escolhido é carregar geometria e spawn de
uma região original nesse host, antes de ampliar refinamentos isolados.
