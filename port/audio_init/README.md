# Preparação dos bancos e sequências de áudio

Este perfil avança de `amInit` até a entrada de `amCreateAudioMgr`, em
`0x80001A10`. O código original prepara o heap de áudio, carrega bancos e
índices da ROM e corrige seus ponteiros. A parada ocorre na chamada do overlay
25 em `+0x64C`, antes de configurar AI, criar a thread de áudio ou produzir
amostras. **Ainda não existe saída de som nem boot completo.**

O perfil contém 198 entradas: 118 funções originais recompiladas, 23 interfaces
de plataforma e 57 funções explicitamente adiadas. Usa o mesmo runtime C/C++
do [bootstrap anterior](../bootstrap/README.md), sem alterar o código do jogo
ou os submódulos. Python prepara e verifica o diagnóstico; os corpos do jogo
executam na biblioteca nativa compilada a partir do C gerado.

## Dados preparados pelo jogo

O arquivo 51 da ROM contém uma tabela de offsets de 48 bytes. O arquivo 52
contém 6.996.800 bytes de bancos, sequências e outros dados de áudio. O jogo
lê apenas as partes necessárias nesta etapa, sem copiar esse arquivo inteiro.

- Banco de sequências: 76.128 bytes, cabeçalho `B1`.
- Banco de efeitos: 58.984 bytes, cabeçalho `B1`.
- Índices de sequências e efeitos: 276 e 6.400 bytes, respectivamente.
- Tabela `S1`: 90 entradas, com endereços de sequência corrigidos para a ROM.
- `seqLen`: comprimentos arredondados para valores pares; o maior é 28.028 bytes.

O auxiliar original de bancos percorre e reloca suas estruturas. Um verificador
separado confere cabeçalhos, ponteiros raiz, limites das seções, índices copiados,
tabela `S1`, comprimentos e o estado do heap. Essa inspeção adicional cobre
campos conhecidos; não é um parser geral de todos os formatos libaudio.

O `ALHeap` tem capacidade de `0x2F990` bytes. Nesta fronteira, ele consumiu
somente 16 bytes para uma alocação alinhada do cabeçalho `S1` de quatro bytes.
A tabela completa é alocada posteriormente por `mmAlloc`, no heap geral do
jogo. Não se deve confundir essa tabela final com a pequena alocação inicial.

## Validação

As [evidências estruturadas](validation.json) registram os artefatos e limites.
Os três parâmetros de TV passaram no Linux x86-64 e no Windows x64 da RTX.
Cada cenário completou 20 transferências, totalizando 167.836 bytes desde o
início de `mainInitGame`, e encerrou suas duas threads. Não houve pedidos PI
rejeitados nem notificações descartadas nesses cenários.

A referência MIPS executou o percurso original até a mesma entrada do gestor
de áudio. A RAM e os rastros de ROM foram iguais nos três cenários; os mesmos
17 GPRs preservados/argumentos/pilha/retorno do perfil anterior foram comparados.
As exclusões de RAM permanecem explícitas: estruturas privadas de threads,
campos de espera das filas e pilhas de chamadas. A comparação de memória
inclui os bancos relocados, além das verificações pontuais independentes.

O oráculo usa conclusão PI imediata, e o runtime usa seu controlador
cooperativo. Não foi comprovada a temporização do console, o processamento
de amostras ou a mistura de áudio. Funções selecionadas e recompiladas que
ficam após a fronteira não são apresentadas como caminhos já validados.

A regressão do bootstrap anterior passou novamente, incluindo os três
cenários, as nove verificações do linker e as três comparações MIPS. A ROM
N64 continua binariamente idêntica à base US.

## Reproduzir

```sh
build/port-recomp/.venv/bin/python port/audio_init/run.py
```

O runner reutiliza o cache do compilador em `build/port-bootstrap/jfg-toolchain`;
os demais artefatos ficam em `build/port-audio-init`. Para Windows:

```sh
cmake -S port/poc -B build/port-audio-init/windows -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DJFG_BOOT_PROFILE=ON -DJFG_THREAD_PROFILE=ON \
  -DJFG_EVENT_PROFILE=ON -DJFG_INIT_PROFILE=ON \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/port/poc/windows-llvm-mingw.cmake" \
  -DJFG_LLVM_MINGW_ROOT="$PWD/build/port-recomp/cross-toolchain/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64" \
  -DJFG_PROOF_DIR="$PWD/build/port-audio-init/proof"
cmake --build build/port-audio-init/windows --parallel 2
python3 port/audio_init/package_windows.py
```

O pacote foi executado por SSH de console no Windows com Python 3.12.10,
`-I -O`, hashes verificados e saída zero. Não inclui ROM, assets, ELF ou código
gerado, e usa a ROM local durante a execução. Processos preexistentes foram
preservados. Os binários do diagnóstico também permanecem fora do Git.

O próximo passo é integrar `amCreateAudioMgr`, começando pelo contrato de
`osAiSetFrequency` e pelas dependências do sintetizador e da thread de áudio.
Essas funções não foram substituídas por retornos fictícios de sucesso.

Infraestrutura desenvolvida com assistência de OpenAI Codex. Nesta etapa,
dois subagentes GPT-6-Sol com esforço médio auditaram dependências e dados;
o coordenador integrou e validou o resultado.
