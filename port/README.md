# Protótipo de recompilação para PC

O alvo escolhido por Thiago é **Windows x64 com GPU NVIDIA**. A primeira opção
gráfica é Direct3D 12, cuja integração ainda depende do suporte específico a
F3DJFG. Esta etapa é uma prova de CPU, sem janela, renderizador, áudio ou jogo
executável completo.

A base do jogo continua sendo `f409c111053c671ae91051a6cdff277e0af7d95e`.
O experimento fica na branch `port/recomp-poc` e não importa alterações
posteriores do projeto original.

## O que o protótipo faz

1. Confere a ROM US pelo SHA-1 e compara com ela as seções utilizadas do ELF.
2. Exporta metadados de 20 funções reais: 13 do programa principal e sete dos
   overlays 3, 6, 18, 19 e 32. As funções são escolhidas explicitamente em
   `poc/prepare.py`.
3. Converte pares HI16/LO16 locais e externos e chamadas JAL ao programa
   principal. Dependências não incluídas e tipos não tratados causam erro.
4. Recompila os bytes MIPS da ROM em C, usando a versão fixada do N64Recomp,
   e compila uma biblioteca nativa de 64 bits.
5. Executa os mesmos casos no código nativo e no Unicorn, configurado como
   MIPS64 R4000 big-endian. Compara os 32 registradores gerais e todos os 8 MiB
   de RAM representada após cada chamada.
6. Para validar as realocações, executa no Unicorn o próprio
   `ProcessRelocationEntry` do JFG, incluindo suas funções auxiliares. O resultado
   esperado não é calculado pela mesma conversão que estamos testando.
7. Testa as funções de overlay em três bases de carga, incluindo casos de
   transporte entre HI16 e LO16 e os endereços de retorno das chamadas.
8. Carrega imagens de módulos na RAM representada, zera a BSS, rejeita
   sobreposições e verifica carga, descarga e recarga. O despachante rejeita
   endereços antigos e dependências ausentes sem encerrar o processo de teste.

O Unicorn é uma dependência de **teste**, não parte proposta da execução do port.
Não existem stubs que devolvem sucesso para chamadas de jogo desconhecidas.

## Resultado inicial — 24/09/2026

- Linux x86-64: diagnóstico de console aprovado e **528 casos diferenciais**
  aprovados nas 16 funções.
- **360 pares** processados pela rotina original de realocação nos testes.
- **96 casos** alteraram a RAM e tiveram o resultado integralmente comparado.
- ROM com hash incorreto, índices inválidos e bases de carga inválidas rejeitados.
- Windows x64: `jfg_poc_smoke.exe` e `jfg_poc.dll` compilados com LLVM-MinGW;
  formato COFF x86-64 e dependências de DLL conferidos. Executados no Windows
  do PC com RTX 5070 Ti: diagnóstico de console aprovado, com código de saída 0
  e hashes do pacote, executável e DLL confirmados.
- O teste Windows executou quatro verificações em `mainGetZBCheck` e
  `mainSetMode`. As 528 comparações diferenciais continuam sendo evidência do
  Linux; a execução da suíte completa no Windows e os testes de GPU ficam para
  uma etapa posterior.

A [evidência estruturada](poc/validation.json) registra os casos, limites, versões
e hashes dos artefatos Windows dessa primeira etapa de 16 funções.

## Ampliação de módulos — 24/09/2026

- **816 comparações diferenciais** aprovadas em 20 funções, incluindo leituras
  e alterações de dados e chamadas dos overlays ao programa principal.
- **792 grupos de realocação** executados pela rotina original: 432 pares
  HI16/LO16 e 360 realocações de chamadas JAL.
- **162 chamadas nativas** tiveram quantidade, destino e endereço de retorno
  comparados com a execução MIPS.
- **15 ciclos de carga, descarga e recarga**, com verificação de cópia da ROM,
  BSS, limites de memória, sobreposição e rejeição de endereços antigos.
- **12 cenários de dependência ausente** retornaram erro controlado. Três
  caminhos condicionais que não usam a dependência continuaram funcionando.

A [evidência desta ampliação](poc/validation-modules.json) é separada da inicial.
Os testes desta etapa foram executados no servidor Linux; os binários Windows
novos foram compilados, mas ainda não executados no PC RTX. A execução Windows
registrada na etapa anterior corresponde exclusivamente aos binários de 16
funções, identificados pelos seus hashes.

## Adaptador de recompilação

`port/recompiler/driver.cpp` usa a biblioteca N64Recomp sem alterar seus fontes.
Nosso frontend interpreta o campo adicional `target_section` nos metadados,
registra as chamadas JAL como referências a símbolos e emite chamadas pelo
despachante. Também preserva o endereço de retorno correspondente à posição em
que o módulo está carregado.

O [perfil de inicialização](boot/README.md) acrescenta captura de destinos
indiretos antes do delay slot, despacho de callbacks e preparação controlada
das quatro instruções alteradas pelo próprio jogo durante a inicialização.

O comando `poc/run.py` compila e utiliza esse frontend. O CLI original do
N64Recomp não interpreta a extensão `target_section` utilizada nesta etapa.

## Inicialização da memória e do carregador — 24/09/2026

O novo perfil em `port/boot` usa o alocador e o `runLink` originais recompilados.
Inicializa suas tabelas, carrega os módulos 19, 6, 32 e 44 e executa o callback
real de inicialização do módulo 44. Inclui 49 funções originais recompiladas,
três interfaces de plataforma e dois caminhos de áudio explicitamente adiados.

Passaram 56 pontos de comparação com a execução MIPS original, incluindo
descarga e recarga, nos limites padrão e estendido do heap. A sequência também
funcionou com o launcher nativo independente do emulador. Os 816 casos da suíte
anterior continuam aprovados. Consulte [escopo e reprodução](boot/README.md) e
[evidência](boot/validation.json). O pacote também foi
[executado no Windows da RTX](boot/validation-windows.json): inicialização
nativa aprovada com os limites padrão e estendido do heap. Nenhum teste de GPU
foi realizado, e isso não transfere para Windows a evidência diferencial Linux.

O [adaptador de filas](runtime/README.md) acrescenta a primeira ligação com os
serviços do N64ModernRuntime. Ele usa o código original desse runtime e compara
o comportamento das mensagens com as rotinas MIPS da ROM.

O [perfil de threads](threads/README.md) conecta essas chamadas a rotinas
originais do jogo: `rcpWaitDP` suspende e retoma por mensagens, enquanto outra
thread pode executar código do JFG. A prova cobre prioridades, criação/início
de threads, cancelamento e fechamento completo dos recursos do teste. As
mensagens gráficas são controladas pelo diagnóstico; ainda não há renderização
nem inicialização completa do jogo.

O [perfil de eventos e timers](events/README.md) já inicia o scheduler original
antes do heap, entrega retraces aos clientes e encaminha PRENMI à rotina de
reset. Relógios controlado e monotônico permitem verificar timers e acordar
threads. O VI ainda não produz imagens; os contadores comprovam o processamento
de mensagens, não renderização.

O [perfil de inicialização e PI](init/README.md) executa o `mainInitGame`
original até sua primeira chamada de bootstrap ainda não resolvida. Prepara
buffers de vídeo e filas do RCP, executa o `romCopy` original por DMA e
descompacta dados reais da ROM. A fronteira atual é `mainInitRlo`, no overlay 36;
o boot completo e a renderização continuam pendentes.

## Limites atuais

- O perfil inicial `poc` mantém seu carregador básico. O perfil `boot` executa
  o carregador real para quatro módulos, mas não cobre todos os módulos,
  dependências entre eles ou todas as formas de modificação de código.
- A chamada indireta do callback está validada. Novos destinos e chamadas entre
  dois overlays ainda exigem ampliação do conjunto registrado e dos testes.
- O perfil básico permanece em uma thread. O novo perfil usa threads nativas
  com execução convidada serializada por agendamento cooperativo. Os efeitos na memória das cargas e
  armazenamentos de ponto flutuante do callback foram comparados, mas não há
  validação geral de FPU, exceções, temporização, boot completo ou campanha.
- O N64ModernRuntime está fixado como dependência. Usamos seu N64Recomp e
  validamos filas e a integração cooperativa com rotinas selecionadas do jogo.
  Os serviços completos do runtime ainda não estão integrados.
- A evidência distingue cada versão testada no Linux e no Windows e a integração
  gráfica ainda pendente. O diagnóstico Windows não inicializa nem testa a GPU.
- O código gerado, as ROMs e os binários ficam em `build/`, fora do Git.

## Dependências reproduzíveis

- N64ModernRuntime: `cdf5abbd5026fef5c364c676e4667c45e42b6863`.
- N64Recomp, submódulo desse runtime: `81213c1831fab2521a6a5459c67b63437d67e253`.
- Python 3.11 ou superior e os pacotes fixados em `poc/requirements.txt`.
- CMake 3.20 ou superior, compilador C17/C++20 e, preferencialmente, Ninja.

As licenças das dependências permanecem nos respectivos submódulos. Esta
preparação não atribui uma nova licença ao código herdado do jogo.

## Executar no Linux x86-64

É necessário ter a ROM US local e o ELF da compilação N64 matching. A preparação
original está documentada no README principal.

```sh
git submodule update --init --recursive -- port/vendor/N64ModernRuntime
python3 -m venv build/port-recomp/.venv
build/port-recomp/.venv/bin/python -m pip install -r port/poc/requirements.txt
build/port-recomp/.venv/bin/python port/poc/run.py
```

O comando produz os arquivos intermediários em `build/port-recomp/`, executa o
diagnóstico de CPU e salva o resultado diferencial em `report.json`. Há logs
individuais para cada etapa. O padrão são 24 casos por função e por base de carga.

## Executar no Windows x64

Use um terminal de desenvolvimento **x64** do Visual Studio 2022, com as
ferramentas de C/C++, CMake, Python e Git. Forneça a ROM US e o ELF correspondente,
que pode ser gerado previamente no Linux ou WSL. Esses arquivos não acompanham
o repositório.

```powershell
git submodule update --init --recursive -- port/vendor/N64ModernRuntime
py -3 -m venv build\port-recomp\.venv
.\build\port-recomp\.venv\Scripts\python.exe -m pip install -r port\poc\requirements.txt
.\build\port-recomp\.venv\Scripts\python.exe port\poc\run.py
```

Use `--elf CAMINHO` e `--rom CAMINHO` se as entradas estiverem fora dos caminhos
padrão. O programa `jfg_poc_smoke.exe` é um diagnóstico de console de algumas
funções, acompanhado de `jfg_poc.dll`. Ele não usa GPU e não abre o jogo.

## Compilação cruzada do diagnóstico Windows

Há um toolchain CMake em `poc/windows-llvm-mingw.cmake`. O compilador utilizado
na preparação inicial é o LLVM-MinGW `20260922`, arquivo
`llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64.tar.xz`, da
[distribuição do projeto](https://github.com/mstorsjo/llvm-mingw/releases/tag/20260922).
SHA-256 do arquivo:

```text
bb7bb7654b33d5aa8712acb837c963b2e0c56352560c76105270a3268c665c21
```

Após gerar as funções pela rotina acima, configure `port/poc` com esse toolchain,
`JFG_LLVM_MINGW_ROOT` apontando para o compilador extraído e `JFG_PROOF_DIR`
apontando para `build/port-recomp/proof`. A compilação cruzada produz arquivos
Windows; ela, sozinha, não comprova sua execução no Windows.

```sh
cmake -S port/poc -B build/port-recomp/windows -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/port/poc/windows-llvm-mingw.cmake" \
  -DJFG_LLVM_MINGW_ROOT="$PWD/build/port-recomp/cross-toolchain/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64" \
  -DJFG_PROOF_DIR="$PWD/build/port-recomp/proof"
cmake --build build/port-recomp/windows --parallel 2
```

## Próximos marcos

O [perfil de bootstrap dinâmico](bootstrap/README.md) já executa o trampolim
original, carrega os overlays 36 e 25 e entra no início de `mainInitRlo`.
Três cenários de bootstrap e nove verificações do linker passaram no Linux
e no Windows x64, com referência MIPS até a entrada de áudio.

O [perfil de dados de áudio](audio_init/README.md) avança por `amInit`, prepara
os bancos e 90 entradas de sequências, e para antes de `amCreateAudioMgr`.
Os três cenários passaram no Linux, na referência MIPS e no Windows x64;
a saída de áudio ainda depende da próxima etapa.

O [perfil de gerenciador de áudio](audio_manager/README.md) já cria o
sintetizador, buffers, filas e a thread de áudio, que permanece sem iniciar.
As três configurações passaram no Linux, contra o MIPS original e no Windows
x64. A próxima fronteira é o construtor de players `n_alCSPNew`.

1. Inicializar os players e avançar até a thread e o processamento de áudio.
2. Ampliar dependências entre overlays e alcançar o boot completo.
3. Provar uma via de renderização compatível com F3DJFG, mantendo o alvo
   Windows/NVIDIA definido por Thiago.
4. Avançar para menu, controles, áudio, saves e uma fase jogável.

Desenvolvimento desta infraestrutura assistido por OpenAI Codex; as verificações
usam execução independente do código MIPS original.
