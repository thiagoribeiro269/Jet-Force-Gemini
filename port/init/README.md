# Inicialização original, buffers de vídeo e leitura da ROM

Este perfil executa o `mainInitGame` original desde sua entrada até a chamada
de bootstrap que ainda não foi integrada. Nesse percurso, o código do jogo
inicia o scheduler, prepara heap e descompressor, aloca os buffers de vídeo,
configura PI, cria as filas do RCP e carrega as tabelas de `runLink`.

A parada é explícita em `0x80044EF8`, na chamada para `TrapDanglingJump` em
`0x80054530`. A tabela original identifica o destino como **`mainInitRlo`,
overlay 36, offset zero, símbolo 1659**. O diagnóstico exige esse destino e
esse ponto exatos; não converte a falta do módulo em um retorno de sucesso.
**O boot e `mainInitGame` completos ainda não terminaram.**

## O que foi integrado

O perfil contém 140 entradas: 91 funções originais recompiladas, 23 interfaces
de plataforma e 26 caminhos explicitamente adiados.

`romCopy` agora executa os bytes originais do jogo neste perfil. A rotina
invalida o cache representado, solicita transferências em partes de até
`0x5000` bytes e espera as mensagens de conclusão. Os perfis anteriores
conservam sua interface síncrona de `romCopy` como referência separada.

O adaptador PI valida os pedidos, preenche os descritores `OSIoMesg` e usa a
fila real do runtime. Prioridade alta entra no início da fila. O controlador
da sessão copia os bytes da ROM local e envia o endereço do descritor à fila
de conclusão, retomando a thread que estava esperando. O descritor pode ser
reutilizado depois que sua transferência termina.

Somente leitura da ROM está habilitada. Escrita, regiões não reconhecidas,
limites inválidos, alinhamento incompatível e reutilização de um descritor
pendente causam erro. Fila de comandos cheia retorna falha da API; fila de
conclusão cheia descarta a notificação, preservando tanto os dados já copiados
quanto a mensagem que ocupava a fila. O fim da sessão cancela trabalho
pendente antes de finalizar suas threads.

`viInit` e suas auxiliares executam as alocações, alinhamentos, cálculos de
escala e preenchimento dos buffers originais. Os testes sujam previamente a
região das futuras alocações para confirmar que a limpeza ocorreu. Os buffers
iniciais são de 320×240 na tabela desta ROM, inclusive quando se testa a
configuração PAL; a reserva de memória e o escalonamento vertical diferem.
Testar essas rotinas com outros valores de TV não comprova o boot da ROM US
em um console PAL.

As flags de VI são armazenadas com a semântica de gamma, divot, dither e
antialiasing correspondente ao contrato de plataforma. Os modos padrão de
VI de 0 a 41 são aceitos. Isso prepara o estado de vídeo, mas **não desenha
uma imagem nem executa RSP/RDP**. `rcpInit` prepara suas filas e referências.

## Dados e descompressão

As funções originais `piRomGetFileSize`, `piRomLoad` e `piRomLoadSection`
consultam a tabela lida da ROM e fazem leituras completas ou parciais. A prova
inclui uma transferência que atravessa o limite de `0x5000` bytes.

A seção 19 contém um fluxo comprimido que produz **153.616 bytes**. O teste
carrega a seção com `piRomLoad`, aloca outra área usando o alocador do jogo e
executa `rzipUncompress`. O resultado é idêntico ao produzido pelo MIPS
original e pelo zlib, byte a byte.

O caminho validado usa entrada e saída em buffers separados. O auxiliar
`piRomLoadCompressed`, marcado `UNUSED` nos fontes, não faz parte deste
perfil. Sua disposição sobreposta, usando o tamanho da seção inteira, não
funcionou com esta entrada: a seção possui bytes após o primeiro fluxo
DEFLATE. O teste sobreposto excedeu o orçamento MIPS e produziu erro de memória
no diagnóstico nativo. Não foi aplicado um patch ao código herdado para
mascarar esse comportamento.

## Compatibilidade observada na inicialização

O JFG consulta sua fila de reset ainda zerada antes da inicialização posterior
dela. A libultra original devolve `-1` para essa leitura não bloqueante antes
de acessar o buffer. O adaptador preserva esse caso exato de fila totalmente
zerada, sem registrá-la como inicializada; outros usos inválidos permanecem
rejeitados. O comportamento foi conferido no percurso MIPS original.

O despachante também passa a registrar o destino e o endereço da chamada que
falhou. A informação é preservada no resultado da thread, permitindo identificar
a fronteira de bootstrap mesmo após o encerramento da chamada recompilada.

## Evidências

A [evidência estruturada](validation.json) identifica versões e artefatos.
O diagnóstico nativo tem sete cenários, com 14 threads criadas e juntadas:

- Inicialização até a fronteira de bootstrap nos três valores de TV.
- Arquivo bruto, leitura parcial e descompressão com conferência por zlib.
- Prioridades de PI, fila cheia, conclusão e rejeição de pedidos inválidos.
- Endereço físico do cartucho e fila de conclusão cheia.
- Encerramento com DMA pendente e uma thread esperando sua conclusão.

Os sete cenários passaram no Linux x86-64 e no Windows x64 da RTX, com saída
zero e 14 threads juntadas em cada execução. No Windows, Python 3.12.10
executou o pacote com `-I -O`, sem emulador, após a conferência dos hashes.

A referência independente executa `mainInitGame` no MIPS original até a
mesma fronteira e compara as leituras da ROM e a RAM inteira, com exclusões
explicitadas para estruturas de threads, listas de espera e pilhas específicas
da execução. Nesse oráculo, a conclusão PI é imediata; no port, ela é entregue
pelo controlador. A comparação confirma o estado final do percurso, não a
temporização ou o intercalamento de interrupções do console.

A descompressão também compara os 32 registradores gerais e a RAM inteira
com MIPS, excluindo somente as áreas reservadas do controlador e da estrutura
da thread nativa. O arquivo descompactado é comparado adicionalmente ao zlib.
Os efeitos dos cálculos de ponto flutuante usados na inicialização de vídeo
estão incluídos na comparação de memória; não há validação geral da FPU.

Uma execução adicional com AddressSanitizer passou nos sete cenários. A
detecção de vazamentos foi desativada (`detect_leaks=0`); a contagem de threads
juntadas é verificada separadamente. Em um processo Python, o teste precisou
pré-carregar tanto libasan quanto libstdc++ para a interceptação de exceções.

As regressões dos perfis anteriores permanecem separadas: 816 casos de CPU,
56 pontos de heap/carregador, 12 cenários de threads, quatro comparações de
`rcpWaitDP`, 15 cenários de eventos/timers e 43 comparações desses serviços.

## Reproduzir

Com o ambiente e as entradas da [prova de CPU](../README.md):

```sh
build/port-recomp/.venv/bin/python port/init/run.py
```

O comando gera o perfil, compila a biblioteca e roda o diagnóstico e a
referência MIPS. Os artefatos ficam em `build/port-init`.

Para preparar Windows x64 depois da geração:

```sh
cmake -S port/poc -B build/port-init/windows -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DJFG_BOOT_PROFILE=ON -DJFG_THREAD_PROFILE=ON -DJFG_EVENT_PROFILE=ON -DJFG_INIT_PROFILE=ON \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/port/poc/windows-llvm-mingw.cmake" \
  -DJFG_LLVM_MINGW_ROOT="$PWD/build/port-recomp/cross-toolchain/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64" \
  -DJFG_PROOF_DIR="$PWD/build/port-init/proof"
cmake --build build/port-init/windows --parallel 2
python3 port/init/package_windows.py
```

O pacote contém DLL, launcher, configuração e hashes; exige Python x64 3.11+
e a ROM fornecida localmente. ROM, ELF, código gerado e arquivos extraídos
permanecem fora do Git. A execução não altera processos ou serviços preexistentes.

## Próxima etapa

Integrar a chamada dinâmica de `TrapDanglingJump`, carregar e registrar o
overlay 36 e continuar por `mainInitRlo`. Áudio, controles, renderização
F3DJFG, saves e uma partida jogável ainda dependem das etapas seguintes.

Infraestrutura desenvolvida com assistência de OpenAI Codex. A base do jogo
e os submódulos permanecem preservados.
