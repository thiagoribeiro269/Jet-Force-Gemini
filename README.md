# Jet Force Gemini — fork com assistência de IA

Este é o fork independente de [Thiago Ribeiro](https://github.com/thiagoribeiro269/Jet-Force-Gemini), derivado do trabalho de [Ryan Myers e colaboradores](https://github.com/Ryan-Myers/Jet-Force-Gemini).

O desenvolvimento deste fork aceita assistência de IA, com revisão das alterações e verificação dos resultados. As decisões e a validação deste fork são independentes das políticas de contribuição do projeto original.

A base escolhida é o commit [`f409c11`](https://github.com/thiagoribeiro269/Jet-Force-Gemini/commit/f409c111053c671ae91051a6cdff277e0af7d95e), que contém a contribuição de Thiago aceita no [PR original #2](https://github.com/Ryan-Myers/Jet-Force-Gemini/pull/2). Atualizações posteriores do original não são incorporadas automaticamente.

Para alterações de descompilação, a referência de validação é recompilar a versão US e comparar o resultado com a ROM base. Uma compilação bem-sucedida, sozinha, não comprova que o binário confere. Um eventual port nativo para PC será uma etapa distinta; este checkout ainda produz uma ROM para N64.

Consulte [a direção e a base do fork](docs/FORK.md) e [as instruções de desenvolvimento](AGENTS.md). ROMs, arquivos extraídos e binários de jogo ficam fora do Git.

Na branch experimental `port/recomp-poc`, a [prova de recompilação para PC](port/README.md) executa funções reais do jogo e compara seus resultados com o MIPS original. O alvo é Windows x64 com GPU NVIDIA; a etapa atual valida CPU e realocações locais, antes da integração gráfica.

## Instruções de compilação herdadas

A repository exploring a decompilation of Jet Force Gemini.

This uses the US ROM by default, but it will also support the kiosk ROM if so desired. Work on PAL and JPN is so far minimal to not started.

This game is a heavily modified Diddy Kong Racing engine, and thus this repository will steal from there when it can.

Grab tools

```sh
git submodule update --init --recursive
```

Install Dependencies
```sh
sudo apt install build-essential pkg-config git python3 wget python3-pip binutils-mips-linux-gnu python3-venv
```

Drop in `us` into the `baseroms` folder as `baserom.us.z64` (sha1sum: `493ced9008dbe932d6e91179b68e8630cf23a023`)
Drop in `kiosk` into the `baseroms` folder as `baserom.kiosk.z64` (sha1sum: `f00f7c7fb085d0df57dcb649793aced5be4e8562`)

```sh
make setup
make extract
make
```
