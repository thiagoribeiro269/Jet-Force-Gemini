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
cena validada e encerramento. A sessão foi executada na RTX. As cenas ainda
são de diagnóstico; o jogo completo, cenário original e física não estão
integrados. As provas individuais passaram a ser regressões do mesmo backend.

## Marcos orientados à integração

| Marco | Critério observável | Situação e dependências |
| --- | --- | --- |
| Estrutura geral e inventário | Carregar duas cenas, atualizar entidades, pausar, trocar cena e liberar referências; renderer recebe snapshots | Concluído no escopo de diagnóstico; cinco regressões GPU preservadas |
| Região original | Geometria/material de uma região da ROM e personagem inseridos no mesmo host, com câmera reproduzível | Próximo: seguir `levelInit`, `trackInit` e carregamento de listas; converter dados diretamente para recursos PC |
| Movimento na região | Deslocamento, gravidade, piso/obstáculos e câmera coerentes | Integrar consultas de colisão e rotinas de controle; substituir movimento e cadência provisórios |
| Interação de gameplay | Arma, projétil, alvo/inimigo, dano e ciclo de criação/remoção | Selecionar um encontro simples da mesma região e ampliar comportamentos/recursos necessários |
| Sessão utilizável | Entrada física, áudio, interface, troca de região e save/load | Integrar serviços nativos, mantendo métodos de acesso ao PC autorizados; nenhum dispositivo virtual de N64 |
| Cobertura e fidelidade | Mais regiões/personagens/efeitos, estabilidade e desempenho medidos | Expandir sobre os mesmos contratos e regressões; aprimoramentos visuais/voz ficam posteriores |

## Ordem do próximo bloco

1. Usar o mapa global para entender `levelInit → trackInit → listas/objetos`,
   seus formatos e dependências; escolher uma região a partir dos dados
   disponíveis, sem supor que ela seja um mesh de personagem.
2. Definir o pacote de recursos da região: geometria, materiais, transformações,
   identificadores e spawn. Manter dados derivados privados e validar limites.
3. Ampliar o catálogo do backend e carregar a região pela sessão existente.
   A primeira imagem deve reunir mundo e personagem no mesmo sistema.
4. Mapear a interface de colisão/controle nesse contexto e escolher o recorte
   de movimento que possa ser conferido por geometria e comportamento.
5. Revalidar a sessão, os assets anteriores e a ROM matching. Registrar
   o que passou, o que foi adaptado e o que continua sem implementação.

Não usar resultados históricos de emulação como comprovação automática da
rota atual. Pedidos que dependem de serviços ainda ausentes devem falhar
explicitamente. Contagem de funções e vídeos de diagnóstico não significam
que uma fase esteja jogável.
