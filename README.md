# PlayTorrio PS4 — Real TV Shows on shadPS4 (v1.1.1)

shadPS4 v0.20.0 currently returns unusable HTTP responses for this app's native PS4 networking. The Windows companion fetcher bypasses that emulator limitation. It fetches real TMDB TV series and movies on the Windows host, then saves their public catalog to the shared PS4 data directory.

## Start loading real shows

1. Install or extract the latest PlayTorrio PS4 v1.1.1 shadPS4 ZIP.
2. Keep your private settings at: %APPDATA%\shadPS4\data\playtorrio\settings.json
3. From the ZIP, double-click **HostTools\Load-Shows.cmd**. No Python or administrator privileges needed.
4. The helper uses your existing tmdbApiKey from settings.json and writes genuine trending and popular TV/movie metadata to: %APPDATA%\shadPS4\data\playtorrio\catalog.json
5. Launch PlayTorrio or press **Square** outside Settings to reload immediately. The app also checks the shared catalog file automatically approximately every 15 seconds.
6. Home displays real trending shows and movies. In Discover, D-pad left/right switches TV/movies, L2/R2 switches pages, and X attempts playback of a selected title when its IMDb ID is available.
7. To refresh repeatedly, run the PowerShell script directly with `-Watch`; otherwise the batch script updates once.

For a **portable or custom shadPS4 data location**, pass the Windows folder mapped to PS4 `/data/playtorrio`, for example `HostTools\\Load-Shows.cmd -DataRoot "D:\\Emulators\\shadPS4\\user\\data\\playtorrio"`. By default the helper targets `%APPDATA%\\shadPS4\\data\\playtorrio`. Ensure the selected folder already contains your private `settings.json`.

Keep githubSyncEnabled set to false. No GitHub requests are necessary to fetch titles. The shared catalog contains public TMDB metadata only, never your TMDB key, addon key, TorBox credentials, or watchlist.

**Limitations:** This is real title/overview/rating browsing, not yet a full poster/image decoder. shadPS4's native PS4 HTTP and AvPlayer compatibility still blocks dependable streaming; catalog loading does not imply functional video playback. On physical GoldHEN hardware, this Windows-host bridge does not apply.

## Private settings JSON

Put only these example fields into your local settings.json (never upload the real keys):

- tmdbApiKey: TMDB v3 key or API read token
- addons: array of your complete HTTPS Stremio manifest.json URLs
- torboxApiKey: optional personal TorBox key
- githubSyncEnabled: false

The diagnostics log is at %APPDATA%\shadPS4\data\playtorrio\logs\diagnostics.log.

See WINDOWS_SETUP.md for Windows paths and troubleshooting.

---

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

On the PS4 Settings tab, press **Square** (or X on Import Settings) to read `playtorrio/settings.json` or `playtorrio-settings.json` from USB0/USB1 and install it at `/data/playtorrio/settings.json` (private file mode). External network configuration and video playback are still under development.

## Native development
```sh
clang++ -std=c++17 -Wall -Wextra -Ivendor src/core.cpp tests/core_test.cpp -o /tmp/ptv3-ps4-core-tests
/tmp/ptv3-ps4-core-tests
# Real PS4 SDK, packaged with OpenOrbis; not available in a normal Linux container
OO_PS4_TOOLCHAIN=/path/to/openorbis bash scripts/build-ps4.sh
```

## Security
The Telegram bot token belongs in the PS4 repository Actions secret `TELEGRAM_BOT_TOKEN`, never source. The GitHub library credential and TorBox API key belong on each individual PS4 only. Do not commit them. Watchlists use a private repository with Contents read/write permissions restricted to it.

## v0.4 — One-run integration tests on your GoldHEN PS4

Install the v0.4 GoldHEN 10.01 experimental PKG. This is **not** a finished movie player, but you can verify real network and configuration integration on your own console.

1. Put `playtorrio/settings.json` on a USB drive, using `config/settings.example.json` as the template. Replace the sample tokens and addon manifest URL **only on your own device**, never in GitHub. The parser also accepts the same `githubRepo`/`githubBranch` used by your Windows/iPad PlayTorrio.
2. Open **Settings** on PS4 and press **Square** to import JSON; the file is stored privately at `/data/playtorrio/settings.json`.
3. Set `diagnosticType` to `movie` or `series`, and `diagnosticId` to a valid Stremio media ID (for series, usually `tt...:season:episode`). This enables the *real* stream-lookup test. Leave it blank to skip looking up a specific stream.
4. For an **existing** TorBox torrent you own or are authorized to access, optionally set `torboxTorrentId` and `torboxFileId` (numeric). The test requests a download link and at most 1 KiB of media data; it does not create or delete torrents.
5. From **any PS4 tab press Triangle** to run the integration suite. Allow time for HTTPS requests. It tests native networking, imported addon manifest URLs and stream responses, short media transfer, TorBox authentication and torrent list, optional TorBox file resolution, private GitHub database read and an SHA-guarded *identical-file* write. It also reports the missing playback engine explicitly.
6. Open **GoldHEN FTP** and retrieve **`/data/playtorrio/logs/diagnostics.log`**. Send that file here so I can diagnose precisely which checks work on your hardware.

**Logs:** `/data/playtorrio/logs/diagnostics.log` (current session), `/data/playtorrio/logs/previous.log` (previous session). Each log line flushes to disk. The log contains only test names, HTTP status/error codes, aggregate counts, and outcomes—**no access tokens, stream URLs, response bodies, account names, media titles, or watchlist contents**. Review any file before sharing.

**Side effect:** The GitHub write-permission check may create a new commit to the database repository, but submits the same JSON bytes guarded by its latest SHA. If another device edits concurrently, it returns a conflict instead of overwriting blindly. The existing watchlist entries are not edited by the diagnostic.

**Known limitations:** There is no native media decoding, audio/subtitle rendering, automatic next-episode playback, or live continuously running library synchronization in this prototype. The tests verify the available API contracts and read/write connectivity—not end-to-end viewing. No console hardware test has been performed by the build server.

### v0.5 watchlist preview
A successful **Triangle** test downloads and validates the private JSON database, displays real library titles under **My List**, and retains an **offline private copy** at `/data/playtorrio/library-cache.json`. This is currently a read-only library view: editing, live background sync and full movie playback require further work. The same diagnostic run verifies GitHub Contents write permission using an identical-file SHA-guarded upload.

## v0.6 — First actual PS4 hardware-video playback path

Unlike earlier diagnostic packages, v0.6 links **libSceAvPlayer** and requests decoded video frames from the PS4 hardware decoder. A native stereo audio-output path is included. The renderer uses CPU NV12-to-RGB conversion: this is an initial functional attempt, not the final smooth GPU interface.

### Start video on PS4
1. On a USB drive, create `playtorrio/settings.json` from `config/settings.example.json`, putting your own credentials **only on your PS4**.
2. Easiest first test: fill `directStreamUrl` with an HTTPS video link to media you are permitted to stream. For addon lookup, instead set `diagnosticId` to an actual Stremio movie ID or episode ID, and add your configured Torrentio `manifest.json` URL. If a Torrentio result is an infoHash and a TorBox key is configured, the resolver checks your TorBox torrents and can add a magnet to your TorBox account.
3. PS4 **Settings → Square** imports the file. You can press **Triangle** for the existing network checks.
4. Go to **Discover** and press **X** on Movies/Series, or go to **Addons** and press **X** to resolve the configured stream. Under **My List**, run Triangle to fetch the library, then press **X** on a saved title to try its Stremio sources.
5. When playback opens, **X/Square** pauses/resumes, **Triangle** hides/shows overlay and **Circle** stops. The player displays only a short status—private video URLs and TorBox credentials are never shown.
6. Retrieve the logs at `/data/playtorrio/logs/diagnostics.log` through GoldHEN FTP. Look for `[RESOLVE]`, `[PLAYBACK]`, and `[AUDIO]`. Especially useful: `PASS first decoded video frame rendered`.

**Important limitations:** Console playback remains **unverified**. The PS4 hardware decoder may reject particular codecs/containers or signed CDN redirects. Basic audio output only supports 48 kHz stereo PCM. Subtitles, English audio-track switching, next-episode autoplay, seeking, and smooth GPU video rendering are still missing. This is not yet equivalent to Windows/iPad PlayTorrio. Use only streams you have permission to access.
