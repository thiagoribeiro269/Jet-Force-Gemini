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
meia-volta e máquina de movimentos das animações. A câmera livre original
também foi portada e orienta o controle como no jogo. Só o roteiro de
entrada ainda substitui o controle físico. [Resultado](native/MOVEMENT.md).
O executável jogável com janela e controle Xbox já existe e aguarda o teste de
Thiago no PC; os testes remotos não abrem a janela. [Como jogar](native/PLAY.md).

## Marcos orientados à integração

| Marco | Critério observável | Situação e dependências |
| --- | --- | --- |
| Estrutura geral e inventário | Carregar duas cenas, atualizar entidades, pausar, trocar cena e liberar referências; renderer recebe snapshots | Concluído no escopo de diagnóstico; cinco regressões GPU preservadas |
| Região original | Geometria/material de uma região da ROM e personagem inseridos no mesmo host, com câmera reproduzível | Concluído no escopo documentado: Forest First + Juno, 180 frames, seis regressões GPU preservadas |
| Movimento na região | Deslocamento, gravidade, piso/obstáculos e câmera coerentes | Concluído para andar e ar do Juno com colisão original |
| Câmera original | Câmera do jogo seguindo o Juno, com os mesmos modos e limites | Concluído para a câmera livre no modo de colisão 1, o de Forest First; zonas, câmeras estáticas, splines, cenas de corte e mira pendentes |
| Entrada física | Controle lido no Windows, valores brutos pelo `joyClamp` original, executável jogável | Implementado: XInput, janela D3D11, parada estrita, gravação e reprodução; janela e controle aguardam o teste de Thiago |
| Estados do Juno em Forest First | Agachar, rolar, deslizar, mira sem tiro, com perfis de colisão originais | Agachar, andar agachado, deslizar e rolar portados; mira (0xB e 5) é o próximo |
| Interação de gameplay | Arma, projétil, alvo/inimigo, dano e ciclo de criação/remoção | Selecionar um encontro simples da mesma região e ampliar comportamentos/recursos necessários |
| Sessão utilizável | Áudio, interface, troca de região e save/load, sobre a entrada física | Integrar serviços nativos, mantendo métodos de acesso ao PC autorizados; nenhum dispositivo virtual de N64 |
| Cobertura e fidelidade | Mais regiões/personagens/efeitos, estabilidade e desempenho medidos | Expandir sobre os mesmos contratos e regressões; aprimoramentos visuais/voz ficam posteriores |

## Ordem do próximo bloco

1. Teste de Thiago com `jfg_native_play.exe` no PC: janela, controle,
   paradas e gravações. As gravações dele definem a ordem dos estados.
2. Estados 0xB e 5: mira com R, ainda sem tiro, com o perfil de câmera e as
   rotações de juntas da mira. Agachar, andar agachado, deslizar e rolar
   (estados 1 e 2) já estão portados.
3. Depois, interação de gameplay na mesma região: tiro, projéteis, objetos e
   inimigos.

Não usar resultados históricos de emulação como comprovação automática da
rota atual. Pedidos que dependem de serviços ainda ausentes devem falhar
explicitamente. Contagem de funções e vídeos de diagnóstico não significam
que uma fase esteja jogável.
