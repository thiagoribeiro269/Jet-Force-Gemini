# Região original no host nativo — plano de 25/09/2026

Plano fechado antes da implementação, a partir de `a4f098c`. O alvo é
renderizar geometria e materiais de uma região original junto do Juno,
usando `NativeSession` e o backend compartilhado. Não abrir janela ou ler
dispositivos; testar por console SSH no Windows RTX. Manter trabalho solo,
sem emulação ou código de emuladores.

## Evidência usada para escolher a região

`levelInit` lê o diretório 0x1E e os cabeçalhos 0x1F. Na versão US, o
cabeçalho observado possui 0x118 bytes: nome nos primeiros 32 bytes,
geometria em +0x54, lista de objetos em +0x56, céu em +0x58 e segunda lista
em +0xCA. A estrutura `LevelHeader` herdada em `include/structs.h` não
descreve esses offsets; não usá-la como especificação deste conversor.

O overlay 24 carrega o diretório 0x24 e os dados 0x25, descompacta RZIP e
resolve os offsets da geometria. O cabeçalho aponta texturas, blocos e caixas;
cada bloco tem 0x48 bytes, vértices de dez bytes, triângulos de 16 e lotes de
16. `func_80014B6C` filtra o bit 0x400 dos lotes nos passes de desenho.

Selecionada **Forest First**, nível 21, geometria 17, listas de objetos
424 e 17. A geometria expandida tem 99.206 bytes, 13 blocos, 3.539 vértices,
2.342 triângulos armazenados e 352 lotes. Seus 45 slots de textura usam
RGBA16/RGBA32, já suportados. O carregador de objetos lê as seções 0x1C/0x1D;
os registros têm tamanho em byte +2, posição em +4/+6/+8 e começam após
um cabeçalho de 16 bytes.

O primeiro registro da lista 424 é o objeto 13, definição 95 (`setuppoint`,
controle 6), em (40,19,841), com campos finais zero. `objSetupPlayers` usa
pontos de controle 6 para preparar o jogador. O objeto 126 resolve para a
definição 1 (`playerBoy`), escala float32 de 0,26 e modelo 220. Serão
conferidos os dados antes da exportação, sem confundir IDs de objeto,
definição, controle, modelo e geometria.

## Pacote a executar

1. Converter a geometria, texturas, lotes visíveis e metadados de entrada
   diretamente para recursos PC privados, com limites e conferência de
   índices/caixas. Auditar os bytes das rotinas usadas na análise. Não
   executar display lists ou o carregador original.
2. Ampliar o catálogo para um recurso de personagem e outro de cenário.
   Preservar os formatos/provas anteriores. O renderer deve desenhar ambos
   por meshes nativos, com endereçamento de textura repetido no terreno.
3. Acrescentar câmera em perspectiva com matrizes próprias do PC e validar
   projeção/profundidade. Usar enquadramento controlado na região; não
   apresentar essa câmera como a implementação original.
4. Preservar a escala e o ponto de entrada documentados. Implementar consulta
   geométrica nativa de altura sob X/Z como base do contrato de colisão.
   Conferir a diferença entre o Y do ponto original e o piso: o raio em
   (40,841) encontra um triângulo em Y=-2. Qualquer alinhamento visual dos
   pés será identificado separadamente; não inventar gravidade original.
5. Carregar cenário e personagem na mesma sessão e produzir capturas com
   a câmera no ambiente. Validar que os recursos são desenhados separada
   e conjuntamente, sem depender só de pixels não pretos.
6. Executar testes de geometria/câmera/recursos, regressões do host e provas
   anteriores na RTX, além do build matching. Publicar fontes, contratos e
   métricas; ROM, meshes, texturas, pontos extraídos e imagens ficam privados.

Ainda ficam pendentes o comportamento dos demais objetos, céu original,
efeitos especiais, câmera/gravidade/colisão completas, áudio, inimigos e
gameplay. A carga desta região é a integração de dados e desenho do mundo,
não a execução completa de `levelInit` ou uma fase já jogável.

## Resultado

Pacote concluído no escopo acima. Forest First e Juno foram renderizados
na mesma sessão D3D11 na RTX: 180 frames distintos, câmera em perspectiva,
2.018 triângulos de cenário e 534 de personagem. Capturas independentes
confirmam a composição; as seis provas anteriores continuam idênticas.
Testes de geometria, câmera e sessão passaram no Linux ASAN/UBSAN e Windows,
e a ROM recompilada permaneceu idêntica byte a byte. Detalhes, limitações
e reprodução em [REGION.md](REGION.md) e [region-validation.json](region-validation.json).
