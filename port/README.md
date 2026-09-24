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
2. Exporta metadados de 16 funções reais: 13 do programa principal e três dos
   overlays 3 e 19. As funções são escolhidas explicitamente em `poc/prepare.py`.
3. Converte três pares de realocações locais HI16/LO16 para o formato do
   N64Recomp. Referências externas e tipos não tratados causam erro.
4. Recompila os bytes MIPS da ROM em C, usando a versão fixada do N64Recomp,
   e compila uma biblioteca nativa de 64 bits.
5. Executa os mesmos casos no código nativo e no Unicorn, configurado como
   MIPS64 R4000 big-endian. Compara os 32 registradores gerais e todos os 8 MiB
   de RAM representada após cada chamada.
6. Para validar as realocações, executa no Unicorn o próprio
   `ProcessRelocationEntry` do JFG, incluindo suas funções auxiliares. O resultado
   esperado não é calculado pela mesma conversão que estamos testando.
7. Testa as funções de overlay em três bases de carga, incluindo casos de
   transporte entre HI16 e LO16, e verifica que um módulo desregistrado não
   continua acessível pelo despachante do protótipo.

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
e hashes dos artefatos Windows.

## Limites desta etapa

- O despachante de teste não implementa todo o `runLink`, seus ciclos de vida,
  referências cruzadas, chamadas de inicialização ou alterações de código.
- A prova cobre funções inteiras e os pares locais selecionados. Não valida
  ponto flutuante, exceções, temporização, boot nem a campanha.
- O N64ModernRuntime está fixado como dependência; nesta etapa usamos seu
  N64Recomp e os cabeçalhos correspondentes. Os serviços completos do runtime
  ainda não estão integrados.
- A evidência distingue a suíte diferencial no Linux, o diagnóstico de console
  já aprovado no Windows e a integração gráfica ainda pendente. O diagnóstico
  Windows não inicializa nem testa a GPU.
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

1. Ampliar a prova para referências entre overlays e chamadas indiretas reais.
2. Integrar carregamento, filas, threads e boot com o runtime completo.
3. Provar uma via de renderização compatível com F3DJFG, mantendo o alvo
   Windows/NVIDIA definido por Thiago.
4. Avançar para menu, controles, áudio, saves e uma fase jogável.

Desenvolvimento desta infraestrutura assistido por OpenAI Codex; as verificações
usam execução independente do código MIPS original.
