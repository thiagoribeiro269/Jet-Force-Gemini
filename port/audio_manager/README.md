# Criação do gerenciador e do sintetizador de áudio

O port agora executa `amCreateAudioMgr`, inicializa o sintetizador original,
prepara buffers e filas e cria a thread `__amMain`. A execução retorna a
`amInit` e para antes de `n_alCSPNew`, chamado pelo auxiliar do overlay 25
em `+0x79C`. **A thread de áudio ainda não foi iniciada, não há amostras
produzidas nem saída de som.**

O perfil contém 211 entradas: 127 funções originais recompiladas, 24 interfaces
de plataforma e 60 funções explicitamente adiadas. A [etapa anterior de dados](../audio_init/README.md)
permanece reproduzível separadamente. Os fontes originais e os vendors não
foram alterados.

## Frequência da interface de áudio

A nova interface host reproduz o contrato observado de `osAiSetFrequency`
nesta ROM. Usa `osViClock` na RAM do jogo, calcula o divisor em precisão simples,
preserva a conversão intermediária do bitrate para oito bits e registra os
valores de DACRATE, BITRATE e CONTROL na ordem original. Uma solicitação
rejeitada pela função original retorna `-1` e preserva esses registradores.

Para o pedido do JFG, de 22.020 Hz, os valores retornados são:

| Parâmetro de TV | Clock representado | Frequência retornada |
| --- | ---: | ---: |
| NTSC, 1 | 48.681.812 | 22.018 Hz |
| PAL, 0 | 49.656.530 | 22.020 Hz |
| MPAL, 2 | 48.628.316 | 22.023 Hz |

O diagnóstico começa em `mainInitGame`. Por isso fornece como estado inicial
a seleção de clock que `__osInitialize` realiza antes dessa função no console.
Não afirma executar a inicialização física completa do N64.

O adaptador aceita frequências de `1` a `UINT32_MAX` com os três clocks acima.
Zero é rejeitado com erro controlado do port (`-14`), sem tentar reproduzir a
conversão indefinida do resultado não finito no código C original. Clocks não
contemplados e chamadas fora da sessão também falham explicitamente.

Essa interface guarda configuração e evidência de escritas; não programa um
dispositivo de som do Windows e não simula consumo de buffers. O encerramento
da sessão limpa o estado antes de finalizar suas próprias threads.

## Estado preparado pelo código original

`n_alInit`, `n_alSynNew` e suas auxiliares alocam e configuram o sintetizador.
Durante a criação das vozes, `alN_PVoiceNew` chama `__amDmaNew` indiretamente;
esse callback também executa código original recompilado e prepara seu estado.
O callback de transferência efetiva de amostras continua adiado.

As três filas de áudio ficam vazias, com capacidades 8, 8 e 76. A thread
`__amMain` é criada e permanece no estado 0, sem iniciar. Seu corpo está
recompilado para fornecer uma entrada chamável ao runtime; handlers de frame,
conclusão e fechamento continuam como dependências explícitas.

Na fronteira de `n_alCSPNew`, o heap de áudio usou 174.112 bytes de sua
capacidade de 194.960 bytes. Esse uso inclui a alocação feita pelo auxiliar
do primeiro player após o retorno do gerente. Bancos, índices e tabelas de
sequências preparados anteriormente continuam sendo conferidos.

## Validação

A [evidência estruturada](validation.json) registra os resultados e artefatos.

- Três cenários de criação passaram no Linux x86-64 e no Windows x64 da RTX.
  Cada cenário encerrou três threads: inicialização, scheduler e áudio.
- A referência MIPS passou nos três parâmetros de TV, comparando 17 GPRs,
  rastros de ROM e RAM, incluindo sintetizador, coeficientes e estado DMA.
  `osAiSetFrequency` executou no MIPS original com as escritas MMIO observadas.
- Trinta comparações específicas de frequência passaram no Linux, incluindo
  rejeições, a conversão de bitrate para oito bits e argumentos unsigned com
  o bit alto definido. Essa suíte diferencial requer Unicorn e não foi
  executada no Windows; o Windows executou os três cenários integrados.
- A regressão da preparação de dados de áudio passou novamente. `make VERSION=us`
  e a comparação binária confirmaram a ROM matching.

As exclusões de RAM são estruturas privadas de threads, ponteiros de espera
das filas e pilhas de chamadas, identificadas no relatório. O estado criado
e não iniciado da thread de áudio é conferido separadamente antes do cleanup.
Os efeitos da FPU na memória desse percurso foram comparados; não há uma
prova geral dos registradores FPR/FCSR ou de todos os modos de arredondamento.
A temporização e a reprodução física de áudio não foram validadas.

## Reproduzir

```sh
build/port-recomp/.venv/bin/python port/audio_manager/run.py
```

O runner usa o cache do recompilador em `build/port-bootstrap/jfg-toolchain`.
Para Windows, após a geração:

```sh
cmake -S port/poc -B build/port-audio-manager/windows -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DJFG_BOOT_PROFILE=ON -DJFG_THREAD_PROFILE=ON \
  -DJFG_EVENT_PROFILE=ON -DJFG_INIT_PROFILE=ON -DJFG_AUDIO_MANAGER_PROFILE=ON \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/port/poc/windows-llvm-mingw.cmake" \
  -DJFG_LLVM_MINGW_ROOT="$PWD/build/port-recomp/cross-toolchain/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64" \
  -DJFG_PROOF_DIR="$PWD/build/port-audio-manager/proof"
cmake --build build/port-audio-manager/windows --parallel 2
python3 port/audio_manager/package_windows.py
```

O pacote foi executado por SSH de console no Windows com Python 3.12.10,
`-I -O`, hashes conferidos e saída zero. A ROM é fornecida localmente. ROM,
assets, ELF, C gerado e binários ficam fora do Git. Nenhum processo ou serviço
preexistente foi encerrado para realizar esses testes.

O próximo passo é inicializar os players de sequências e efeitos e avançar
até o início controlado da thread de áudio. Processamento de amostras/RSP,
saída de som, renderização, controles e boot completo continuam pendentes.

Infraestrutura desenvolvida com assistência de OpenAI Codex. Dois subagentes
GPT-6-Sol com esforço médio auditaram o gestor e implementaram o contrato de
frequência e sua referência; o coordenador integrou e validou a etapa.
