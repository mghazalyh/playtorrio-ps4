# PlayTorrio PS4 — native homebrew

Platform target: GoldHEN-enabled PS4 running firmware 10.01.

The PS4 app is a **separate native client**. Feature contracts are tracked in [FEATURE_PARITY.md](FEATURE_PARITY.md) against [mghazalyh/ptv3](https://github.com/mghazalyh/ptv3). The private data repository [torriodb](https://github.com/mghazalyh/torriodb) uses the same `playtorrio/watchlist-v1.json` format.

**Current stage: prototype, not a working streaming release.** A compiled PKG alone does not imply TorBox playback, MPV, or full PS4 UI functionality. Only run packages labeled as experimental.

- `src/core.cpp`: settings JSON import, safe manifest URL normalization, Stremio stream parsing, TorBox request URL / success parser, GitHub watchlist schema, tombstones, SHA update body.
- `tests/core_test.cpp`: native host regression tests, no tokens/network needed.
- `ps4/main.cpp`: installable native app shell (controller-driven screens; no video decoding).
- `scripts/build-ps4.sh`: OpenOrbis cross-build/packaging. Requires official OpenOrbis toolchain.
- `.github/workflows/ps4.yml`: tests, experimental PKG build, artifacts and Telegram upload if configured.

## Account and settings import
Set up `/data/playtorrio/settings.json` **using PS4 FTP** (GoldHEN) or copy it through a homebrew file manager. See `config/settings.example.json`. This file and all tokens must remain on the console. Settings importer accepts addon `manifestUrl`, `transportUrl` and `stremio://` URLs, and GitHub repo/branch options; credentials never go into public GitHub.

A full settings import UI and the native network/playback adapter are still in development.

## Native development
```sh
clang++ -std=c++17 -Wall -Wextra -Ivendor src/core.cpp tests/core_test.cpp -o /tmp/ptv3-ps4-core-tests
/tmp/ptv3-ps4-core-tests
# Real PS4 SDK, packaged with OpenOrbis; not available in a normal Linux container
OO_PS4_TOOLCHAIN=/path/to/openorbis bash scripts/build-ps4.sh
```

## Security
The Telegram bot token belongs in the PS4 repository Actions secret `TELEGRAM_BOT_TOKEN`, never source. The GitHub library credential and TorBox API key belong on each individual PS4 only. Do not commit them. Watchlists use a private repository with Contents read/write permissions restricted to it.
