# Inicialização nativa da memória e do carregador

Este perfil executa um trecho real da inicialização do JFG: preparação de
instruções pelo próprio jogo, criação do heap, inicialização das tabelas de
`runLink` e carga de quatro módulos. As rotinas do jogo são recompiladas para
x64; o launcher `native.py` usa apenas a biblioteca padrão do Python. Ele não
executa um emulador e não inicia `boot`, `mainThread`, vídeo ou uma partida.

## Código executado e interfaces

A preparação reúne **54 entradas**: **49 funções originais recompiladas**,
três interfaces explícitas de plataforma e duas dependências adiadas. Essa
contagem representa o código incluído, não uma cobertura de todos os caminhos.

A sequência é `RevealReturnAddresses`, `mmInit`, `runlinkInitialise` e
`runlinkDownloadCode` para os módulos 19, 6, 32 e 44. O alocador e o carregador
originais escolhem os endereços, copiam as imagens, zeram BSS, aplicam suas
tabelas de realocação e mantêm os registros do jogo. O despachante acompanha
esses registros para as seções incluídas neste perfil.

O módulo 44 chama `_AutoInit00044` por um endereço calculado em tempo de
execução; ele chama `amAudioLinesReset`. Essa rotina realmente prepara sua
memória. Não produz som. Os caminhos `amSndStop` e `amAmbientStop`, que seriam
necessários para parar sons ativos, permanecem sem implementação neste perfil
e retornam erro controlado quando atingidos.

As únicas interfaces substituídas por contratos do computador são:

| Interface | Comportamento neste perfil |
| --- | --- |
| `romCopy` | Copia os bytes solicitados da ROM local para a RAM representada, de forma síncrona e com verificação de limites. |
| `osWritebackDCache` | Registra a operação e mantém a ordenação do compilador sobre a memória coerente da CPU. |
| `osInvalICache` | Mesmo contrato de coerência; não simula cache do N64, temporização ou sincronização de GPU. |

`RevealReturnAddresses` encontra quatro instruções com marcadores em cinco
regiões candidatas. Uma entrada não contém o marcador nesta ROM e permanece
inalterada, como na rotina original. O recompilador incorpora as quatro
instruções resultantes, mas exige que a preparação original já tenha acontecido
na RAM antes de chamar as funções afetadas. A ROM de entrada é preservada.

## Validação

- **56 pontos de comparação** contra a execução MIPS original: 28 com o limite
  padrão do heap em `0x80400000` e 28 com o limite estendido em `0x80600000`.
  A RAM representada tem 8 MiB em ambos os casos.
- Comparação dos 32 registradores gerais, dos 8 MiB completos de RAM e da
  sequência de chamadas de ROM/cache após cada ponto.
- Tabela original com 158 posições, carga dos quatro módulos, callback real,
  consultas, uso de funções carregadas, descarga, recarga e carga repetida sem
  nova leitura da ROM.
- Conferência da região de código inteira após a preparação das quatro
  instruções, duas rejeições de uso prematuro do alocador e quatro rejeições
  dos caminhos de áudio adiados, em cenários negativos separados.
- A suíte anterior de 20 funções continua com **816 casos aprovados**.

O oráculo MIPS usa os mesmos três contratos de plataforma descritos acima.
Assim, esta comparação valida os algoritmos do jogo sob esses contratos, sem
alegar equivalência de DMA, caches ou temporização com o console físico.

A rotina de áudio usa cargas e armazenamentos de registradores de ponto
flutuante. Seus efeitos na memória foram comparados; os registradores de ponto
flutuante não são comparados diretamente e não há validação geral de FPU.

A [evidência estruturada](validation.json) distingue execução Linux, compilação
Windows e limites. O novo pacote Windows ainda precisa de execução nessa
plataforma. O diagnóstico inicial já executado na RTX pertence a outra versão.

## Reproduzir

Após preparar o ELF N64 matching, a ROM US e as dependências da
[prova inicial](../README.md):

```sh
build/port-recomp/.venv/bin/python port/boot/run.py
```

O comando prepara o perfil, compila o adaptador e a biblioteca, executa os
testes e salva `build/port-boot/report.json`. Para executar somente a sequência
nativa, sem os pacotes de teste:

```sh
python3 port/boot/native.py \
  --rom baseroms/baserom.us.z64 \
  --manifest build/port-boot/proof/manifest.json \
  --library build/port-boot/native/libjfg_poc.so \
  --report build/port-boot/native-only-report.json
```

Use `--extended` para selecionar o limite estendido do heap. Isso ainda não
equivale a iniciar o jogo na configuração completa de um Expansion Pak.

## Preparar o diagnóstico Windows x64

Depois de gerar o perfil acima, use o toolchain já descrito na prova inicial:

```sh
cmake -S port/poc -B build/port-boot/windows -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DJFG_BOOT_PROFILE=ON \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/port/poc/windows-llvm-mingw.cmake" \
  -DJFG_LLVM_MINGW_ROOT="$PWD/build/port-recomp/cross-toolchain/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64" \
  -DJFG_PROOF_DIR="$PWD/build/port-boot/proof"
cmake --build build/port-boot/windows --parallel 2
python3 port/boot/package_windows.py
```

O pacote local contém DLL, launcher, configuração, hashes, instruções e um
diagnóstico simples de console. Não contém ROM, ELF nem caminhos privados.
No Windows, o launcher exige Python x64 3.11+ e a ROM fornecida localmente.
O `jfg_poc_smoke.exe` sozinho continua testando apenas quatro verificações
simples em duas funções; ele não executa a inicialização do carregador.

Ainda faltam integração com threads e filas, inicialização completa, mais
dependências entre módulos, gráficos F3DJFG, áudio, controles e saves. O perfil
atual opera em uma única thread. Funções do jogo fora do conjunto registrado
não ficam executáveis apenas porque seus módulos foram carregados.

Infraestrutura desenvolvida com assistência de OpenAI Codex. Os fontes do jogo
e dos fornecedores permanecem na base escolhida, sem alterações.
