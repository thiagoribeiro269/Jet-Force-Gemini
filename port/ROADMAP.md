# Roteiro do port Windows x64

A direção vigente é integrar o fluxo completo do jogo sobre uma estrutura
nativa. Thiago priorizou o contexto global em 25/09/2026, mantendo trabalho
solo, Windows/NVIDIA e a exclusão de RT64, emulação e código de emuladores.
Android permanece fora do escopo. A base continua `f409c11`; não sincronizar
o projeto original automaticamente.

A [arquitetura geral](native/ARCHITECTURE.md) relaciona o fluxo original,
os contratos do PC, o inventário estático e as fronteiras ainda pendentes.
O [roteiro anterior](ROADMAP-legacy.md) é somente histórico.

## Ponto atual

O programa identificado pelo ELF tem 2.916 funções com tamanho definido e
25 entradas sem extensão conhecida, em 156 seções de módulo. O inventário
confere os bytes contra a ROM e resolve 14.010 relações diretas, incluindo
as chamadas através de `TrapDanglingJump`. Destinos indiretos e especiais
estão explicitamente pendentes; esse mapa não mede percentual de conclusão.

O host nativo já possui recursos separados do backend gráfico, ciclo em
passos fixos, snapshots, entidades independentes, pausa/retomada, troca de
cena validada e encerramento. Em Forest First, o Juno agora se move pelo
código original portado: controle, gravidade, pulo, colisão com o cenário,
meia-volta e máquina de movimentos das animações. A câmera da prova e o
roteiro de entrada ainda são do port. [Resultado do movimento](native/MOVEMENT.md).

## Marcos orientados à integração

| Marco | Critério observável | Situação e dependências |
| --- | --- | --- |
| Estrutura geral e inventário | Carregar duas cenas, atualizar entidades, pausar, trocar cena e liberar referências; renderer recebe snapshots | Concluído no escopo de diagnóstico; cinco regressões GPU preservadas |
| Região original | Geometria/material de uma região da ROM e personagem inseridos no mesmo host, com câmera reproduzível | Concluído no escopo documentado: Forest First + Juno, 180 frames, seis regressões GPU preservadas |
| Movimento na região | Deslocamento, gravidade, piso/obstáculos e câmera coerentes | Concluído para andar e ar do Juno com colisão original; câmera original pendente |
| Câmera original | Câmera do jogo seguindo o Juno, com os mesmos modos e limites | Portar `camera.c` e as rotinas de câmera de `charControl` sobre o host; próximo bloco |
| Interação de gameplay | Arma, projétil, alvo/inimigo, dano e ciclo de criação/remoção | Selecionar um encontro simples da mesma região e ampliar comportamentos/recursos necessários |
| Sessão utilizável | Entrada física, áudio, interface, troca de região e save/load | Integrar serviços nativos, mantendo métodos de acesso ao PC autorizados; nenhum dispositivo virtual de N64 |
| Cobertura e fidelidade | Mais regiões/personagens/efeitos, estabilidade e desempenho medidos | Expandir sobre os mesmos contratos e regressões; aprimoramentos visuais/voz ficam posteriores |

## Ordem do próximo bloco

1. Mapear o sistema de câmera original: `camTick`, as rotinas de câmera de
   `charControl` (`func_8002CF78`, `cameraTopDown`, `cameraGetBlend` e
   vizinhas), os modos e as câmeras estática, de spline e de objeto.
2. Portar o modo usado em Forest First, com a colisão de câmera contra o
   cenário, e expor `controlcam` à entrada do Juno como no jogo.
3. Substituir a câmera do port na prova de movimento e preservar as
   regressões anteriores byte a byte.
4. Em seguida, entrada física: ler um controle no Windows e passar os valores
   brutos pelo `joyClamp` original, num executável jogável aberto por Thiago.
5. Depois, interação de gameplay na mesma região.

Não usar resultados históricos de emulação como comprovação automática da
rota atual. Pedidos que dependem de serviços ainda ausentes devem falhar
explicitamente. Contagem de funções e vídeos de diagnóstico não significam
que uma fase esteja jogável.
