# handover.md — PlayTorrio PS4 project continuity / Codex takeover

> **Purpose:** The persistent, source-backed handover after chat-session loss. Detailed enough for Codex or another agent to resume with no dependence on the previous chat. Read root **AGENTS.md** first. Keep this document current on **every meaningful coding/debugging/release task**; append a Change Log entry and refresh the snapshot. It is intentionally long because several confirmed root causes were found through multiple experimental builds.

## 0. CURRENT STATE — READ THIS FIRST

**Snapshot date:** 2026-10-27 (Singapore time, UTC+08).  
**GitHub repository:** https://github.com/mghazalyh/playtorrio-ps4  
**Default branch:** main, public.  
**Latest implemented feature commit before these handover files:** a887b6c710373c5c374b6eba6d3e9b9e8c594526 (codec-aware source preference, v1.3.3).  
**Last tested published build:** ps4-native-playback-1.3.3, https://github.com/mghazalyh/playtorrio-ps4/releases/tag/ps4-native-playback-1.3.3  
**CI run:** https://github.com/mghazalyh/playtorrio-ps4/actions/runs/40375602957 — completed successfully: portable C++ tests, OpenOrbis cross-compilation, experimental PS4 package and emulator ZIP validation.  
**User's most recent actual emulator playback test:** **v1.3.1**, not v1.3.3. It resolved a video, verified the 2.43 GB media file supported HEAD and HTTP 206 Range, and AvPlayer accepted the source, but **no video appeared**; screen displayed “Buffering stream…” (see Section 7).  
**Current primary blocker:** **first decoded/rendered video frame has NOT been demonstrated**. Audio and subtitle playback remain unverified/unimplemented to final quality. **v1.3.3 has not yet been verified on the user's shadPS4.**  
**Top next step:** test v1.3.3 playback and inspect new **VIDEO_IO**/**PLAYBACK** log lines (callback invocation, read count, range chunk count, 60-second stall or first frame). Diagnose the exact decoder/pipeline stage, do not restart TMDB/networking investigation.

**Verified in emulator:** native HTTP(S) requests, TMDB trending JSON, Stremio manifest, TorBox authenticated APIs, real catalog, Stremio source resolution, and range-compatible media access.  
**Verified in CI:** C++ regression tests, PS4 package format, extracted emulator game ZIP content.  
**Not verified:** decoded video frame, continuous playback, synchronized audible sound, rendered subtitles, 1080p performance, **any tests on a physical PS4**, and actual end-user playable final release.

**User directive:** Physical PS4 (GoldHEN, target firmware 10.01) is the product target. shadPS4 is the test/development environment. Share common core functionality, isolate emulator quirks, preserve a real-hardware-compatible video architecture. **Do not turn HostTools or other Windows helpers into a permanent console dependency.**  
**GitHub watchlist rule:** githubSyncEnabled must remain **false** and GitHub watchlist read/write disabled until the user requests otherwise.

### Quick resume command sequence

~~~sh
git status --short
git branch --show-current
git log -10 --oneline
cat AGENTS.md
cat handover.md
# Review the latest PS4 CI and release online:
# https://github.com/mghazalyh/playtorrio-ps4/actions
# https://github.com/mghazalyh/playtorrio-ps4/releases
~~~

The documentation-creation commit necessarily changes HEAD after a887b6c; treat **a887b6c** as the specific last feature commit, **not** a forever-current branch SHA.

### Current downloadable artifacts

- Emulator-extracted ZIP: https://github.com/mghazalyh/playtorrio-ps4/releases/download/ps4-native-playback-1.3.3/PlayTorrio-PS4-shadPS4-extracted.zip (verified released, 663,791 bytes as checked on 2026-10-27).
- Experimental PKG: https://github.com/mghazalyh/playtorrio-ps4/releases/download/ps4-native-playback-1.3.3/PlayTorrio-PS4-GoldHEN-10.01-EXPERIMENTAL.pkg (verified released, 6,619,136 bytes).
- The ZIP contains the install-ready **BREW20346/** folder; Windows **HostTools/** are included but optional. The PKG is NOT proof of a working hardware video player.
- Build's user-facing package metadata: TITLE_ID **BREW20346**, CONTENT_ID **IV0000-BREW20346_00-PLAYTORRIOPS4APP**, APP_VER and VERSION **01.33**.

## 1. Why this repository exists / ownership and intent

PlayTorrio PS4 is a separate native C++ PS4 client, not a Flutter app packaged for PS4. Behavioral reference is the user's **mghazalyh/ptv3** project, and the private watchlist backend is **mghazalyh/torriodb** (schema playtorrio/watchlist-v1.json). Neither upstream app nor private watchlist should be modified for this project unless explicitly requested.

Original / ongoing product goals:
- Native big-screen TV-oriented PlayTorrio UI with DualShock 4 controls.
- Discover real movies/series and metadata through TMDB.
- Parse installed Stremio-compatible addon manifests and request per-title stream sources.
- Use TorBox authentication / debrid for authorized source resolution as applicable.
- Play movies/TV episodes **on a physical PS4**, with audio, English tracks/subtitles and next-episode behavior eventually.
- Optionally read/synchronize the PlayTorrio watchlist with the upstream JSON schema **only when expressly enabled**; owner currently requests it disabled.
- Native console execution with GoldHEN; shadPS4 emulator is the iterative validation loop.
- Supply both a GoldHEN experimental PKG and shadPS4-friendly unpacked game ZIP.

User interaction preferences in this project:
- Focus on actually working PS4 software, objective error analysis and concrete tests.
- Be direct and precise, usually concise bullet points in chat; this handover can be lengthy.
- Always distinguish installed/runnable/tested versus proposed/compiled.
- No unnecessary questions when enough logs/code are present.
- Do not confuse “PlayTorrio app prints PASS” with real playback proof.
- Treat the latest user's request as the immediate task; update this handover after completing it.

## 2. Architecture and source map (as of v1.3.3)

### Portable core (host-tested, not PS4-specific)

- **src/core.hpp / src/core.cpp**: ptv::Settings fields, JSON import, manifest URL normalization (HTTPS only), addon manifest parser, Stremio stream URL, stream list parser, TorBox link response helpers, GitHub watchlist JSON merge/tombstones/base64 and contents update schema. Settings default **github_sync_enabled=false**.
- **src/config_loader.hpp / .cpp**: bounded 256 KiB C stdio settings loader (deliberately deferred until after first frame); understands UTF-8 BOM, rejects UTF-16/NUL/oversize/malformed JSON; protects last valid in-memory settings. Status names LOADED, MISSING, EMPTY, TOO_LARGE, INVALID_ENCODING, INVALID_JSON, READ_ERROR.
- **src/catalog.hpp / .cpp**: local catalog.json and native TMDB JSON parsing into ptvcatalog::Catalog and Item. Each item has TMDB ID, title, overview, year, rating, optional IMDb ID and TV/movie type. Up to 40 per category. Invalid IDs and unsafe text are skipped/sanitized. Current text display is ASCII-centric; non-ASCII UTF-8 glyph bytes are replaced rather than correctly shaped.
- **src/playback_choice.hpp**: ptvplayback::score heuristic introduced in v1.3.3. Scores HTTPS streams based on addon-provided title/name and file URL extension; prefers labeled H.264, AAC, 720/1080p, MP4/M4V over 4K/HDR/HEVC. Rejects known AV1/VP9, HLS .m3u8 and non-HTTPS. **These are untrusted hints, not verified codecs.**

### PS4 native

- **ps4/main.cpp**: PS4 executable entry point, scene/font setup, controller navigation across Home/Discover/My List/Addons/Settings, first-frame checkpoints, deferred settings worker, native catalog worker, local catalog reload, stream resolver worker, video/overlay frame loop and UI state.
- **ps4/native_net.hpp / .cpp**: PS4 sceNet/sceSsl/sceHttp initialization; PS4 HTTP template/connection/request; request headers, read limits, status, underlying errno, response headers (Content-Length/Content-Range/Accept-Ranges), optional redirect following on media HEAD and Range requests.
- **ps4/diagnostics.hpp / .cpp**: user-triggered integration suite, private redacted log, status/counters and UI My List preview; log rotation on startup. Public HTTPS probe plus deliberate nonexistent-page test, response format/size/fingerprint without bodies.
- **ps4/catalog_online.hpp / .cpp**: native TMDB weekly trending TV/movies plus popular sources, parsed and deduplicated. Initial 6 per category prefetch IMDb external IDs; later pages resolve selected title's IMDb ID lazily in the background. Uses TMDB API key or Bearer token locally.
- **ps4/source_resolver.hpp / .cpp**: direct configured URL → existing TorBox torrent/file → Stremio addon stream → TorBox hash/magnet flow; selects a stream by heuristic score in v1.3.3. Optional TorBox operations are not side-effect-free (creating a magnet can enqueue a torrent).
- **ps4/video_player.hpp / .cpp**: ps4video::Player; native AvPlayer library initialisation, file-replacement callbacks (open/close/readOffset/size), HTTPS HEAD + 64 KiB HTTP 206 probe, bounded 256 KiB Range cache, AvPlayer AddSource using synthetic /ptvremote.mp4, decoded NV12 pixel loop drawn with Scene2D, AudioOut thread, basic pause/resume/close; no first frame confirmed. v1.3.2 added 60-second startup timeout and counts read callback/range requests.
- **ps4/avplayer_abi.hpp**: manually typed ABI subset because OpenOrbis AvPlayer header has placeholder declarations. In v1.3.2 corrected boolean return signatures of IsActive, GetVideoDataEx and GetAudioData according to shadPS4 source. ABI and callback compatibility on real PS4 must remain an explicit test item.

### Optional host helper / packaging / checks

- **HostTools/Load-Shows.cmd**, **HostTools/Refresh-Catalog.ps1**: Windows PowerShell requests TMDB over the host OS and atomically writes **/data/playtorrio/catalog.json** via shadPS4's shared filesystem. Introduced before the emulator network misconfiguration was found; **optional fallback only**. Supports a custom **-DataRoot** for portable shadPS4 installs, **-Watch** refresh mode; no Python/admin needed.
- **scripts/build-ps4.sh**, **scripts/patch-graphics.py**: OpenOrbis PS4 C++17 cross-compile, native runtime/font assets, FreeType framebuffer compatibility patch, PKG and unpacked emulator ZIP generation.
- **vendor/picojson.h**: vendored JSON parser. Ensure code remains C++17 compatible with OpenOrbis libc++.
- **tests/core_test.cpp**, **config_loader_test.cpp**, **catalog_test.cpp**, **playback_choice_test.cpp**, **windows_catalog_test.ps1**: host checks (the last tests PowerShell with mocked TMDB without network or secrets).
- **.github/workflows/ps4.yml**: portable tests, OpenOrbis build, format/content verification, upload artifacts, GitHub release and conditional Telegram.
- **.github/workflows/windows-catalog-test.yml**: path-triggered Windows mocked bridge test.
- **.github/workflows/shadps4-smoke.yml**: older experimental headless Vulkan emulator smoke harness against a fixed v0.6 release, not current v1.3.3 playback validation.
- **.github/workflows/feature-watch.yml**: weekly parity issue/watch against the user's ptv3 reference; no copying Flutter app.
- **FEATURE_PARITY.md**, **README.md**, **WINDOWS_SETUP.md**: useful history, but **some sections are obsolete** and contradict later improvements. Use this handover plus code as current baseline.

## 3. Configuration, files, controllers, diagnostics

### Real device and emulator file layout

PS4 virtual data:
- **/data/playtorrio/settings.json** — private local credentials and preferences; never commit.
- **/data/playtorrio/catalog.json** — optional public TMDB metadata cache from HostTools.
- **/data/playtorrio/logs/diagnostics.log** — current boot's redacted app log.
- **/data/playtorrio/logs/previous.log** — previous boot's diagnostic log.
- **/data/playtorrio/library-cache.json** — historical offline copy of private watchlist, relevant only if library access enabled.

Windows shadPS4 default maps **/data** to **%APPDATA%\shadPS4\data**, so settings/logs live under **%APPDATA%\shadPS4\data\playtorrio**. Portable emulator can instead map **user\data** beneath its own directory; check the actual emulator user folder. Previous user had settings successfully loading from this mapping.

Typical local settings schema (all values below are placeholders; never commit real keys):

~~~json
{
  "addons": ["https://YOUR-ADDON-URL/manifest.json"],
  "tmdbApiKey": "YOUR_PRIVATE_TMDB_KEY_OR_READ_TOKEN",
  "torboxApiKey": "YOUR_PRIVATE_TORBOX_KEY",
  "githubSyncEnabled": false,
  "githubToken": "",
  "englishAudio": true,
  "englishSubtitles": true,
  "diagnosticType": "movie",
  "diagnosticId": "",
  "directStreamUrl": ""
}
~~~

Also supported in portable settings: githubRepo/githubBranch (keep sync off), optional existing numeric torboxTorrentId/torboxFileId, alternative addon containers and manifest URL keys, underscore aliases for certain token fields. The sample tracked at **config/settings.example.json** contains only placeholders.

Native media selection: selected movies use IMDb **tt...**; series pass **tt...:season:episode**, currently initially **:1:1**. On-demand TMDB external_id requests fill missing IMDb ID for later Discover pages. If diagnosticId is blank, the Triangle stream test **SKIPs** individual media stream lookup; this is expected and **not** proof playback can't resolve a selected title.

### Buttons (current intended controls; actual hardware behavior requires confirmation)

- **Triangle**: run diagnostic suite when browsing; toggle video overlay during playback.
- **X/Cross**: activate/select a title/action. During video playback X (and Square) pause/resume.
- **Circle**: leave video playback/back.
- **Square**: settings import/reload in Settings; trigger native TMDB and optional local catalog refresh in browsing screens.
- **D-pad**: navigate tabs/cards. On Discover, left/right TV versus movie category; **L2/R2** page navigation.
- UI targets 1280×720. Native graphics use OpenOrbis Scene2D with bundled licensed Gontserrat font. Fonts, FreeType pixel blending and framebuffer initialization caused several early emulator rendering issues, now historically improved.

### Safe app diagnostics

- Log is line-oriented, timestamped Unix seconds, labels such as BOOT, RENDER, SETTINGS, HTTPS, HTTP_PROBE, HTTP_404_CONTROL, TMDB, ADDONS, STREAM, TORBOX, TORBOX_DL, CATALOG, RESOLVE, VIDEO_IO, PLAYBACK, AUDIO, SUBTITLES, GITHUB, SUMMARY.
- Never include API secrets, signed URLs, full request bodies, full response bodies, media titles, user/account names, or private watchlist contents.
- **Do not indiscriminately publish shadPS4's own libSceHttp debug log**; emulator source may log full URLs including TMDB query keys.
- Watch logs for **PASS first decoded video frame rendered**; without that line, do not claim video success.

## 4. Chronological history of development and evidence

All work described below occurred primarily **2026-10-26 UTC / 2026-10-27 Singapore time**. Release versions are experimental labels, not full production readiness.

### Stage A — Native core, UI and first installable PS4 packages (v0.1–v0.3)

1. Repository initialized as separate PS4 homebrew: commit **2134516e56**.
2. Portable ptv::Settings, JSON/Stremio/TorBox/watchlist core and host tests implemented: **f831a6c8a8**.
3. Native OpenOrbis application and packaging added: **708193a39c**, **13b8491a3c**, and workflow **0edff97db0**. Official tools require checksum-pinned SDK, not ordinary native Linux compilation.
4. DualShock-controlled on-screen dashboard and local settings/USB import: **06f42832a4**, **54489c5316**. Licensed TrueType/free-type assets included.
5. v0.1 prototype release **ps4-prototype-0.1.0**, followed by UI preview releases **ps4-ui-preview-0.2.0** and **ps4-ui-preview-0.3.0**; dense 720p layout and accessible tile grid evolved.
6. Initial CI packaged binaries; **no actual PS4 physical console test** followed from CI alone.

### Stage B — Live diagnostics, My List preview, playback alpha (v0.4–v0.6)

1. Native HTTP client, bounded responses, sceNet/sceSsl/sceHttp integration **e1b457b294**; logging/integration checks **5cf33db32a**, controller Triangle diagnostics **b0d0717c30**.
2. v0.4 diagnostics package **0612fbcd4a** with FTP-readable logs (current BOOT log still identifies diagnostics “v0.4”; this is a log format label, **not app version**).
3. Optional diagnosticId, diagnosticType, TorBox existing file selectors for targeted stream and CDN checks.
4. Validated private GitHub watchlist GET/preview/cache was implemented historically for v0.5. Later user explicitly directed **GitHub sync OFF**. Its code may remain but must not run when disabled.
5. v0.6 added resolver for direct sources, Stremio and TorBox magnet, and **libSceAvPlayer** decoded NV12/video output plus basic PS4 AudioOut. Core commits include **374c65b37d**, **dd32883c4f**, package **28816c358b**, release **ps4-video-alpha-0.6.0**.
6. Key limitations acknowledged: audio only on certain PCM formats, no full subtitles, no GPU-based video renderer/next episode, actual video unverified.

### Stage C — Emulator packaging, presentation and settings safety (v0.7–v1.0)

1. shadPS4 QtLauncher installations required unpacked game files instead of trying to install an experimental PKG. Added **BREW20346/** ZIP alongside PKG: **9f53d8c3d3**, **6139e7d94a**.
2. v0.7 visual fix: font signed glyph coverage, safe FreeType rendering, 720p cinema UI, status bar overlap. Release **ps4-visual-fix-0.7.0**.
3. v0.8 local settings and TMDB key configuration, strong default opt-out of GitHub watchlist. Commits **097ae3fcef**, **467ccfff3e**, **ede23eb969**; shadPS4 settings guide.
4. v0.9 black-screen/first-frame fix: avoid framebuffer reads during glyph blending, always clear screen, log render/flip stages (**c6d7defd6b**, **742dc31dafa**). This fixed certain display startup pitfalls.
5. v1.0 crash-safe deferred settings loading: before that, settings C++ iostream import could interfere with early emulator video startup. Bounded C stdio loader, UTF-8 BOM support, exact errors and deferred worker implemented (**8702f3d167**, **593a0130fb**); release **ps4-settings-safe-1.0.0**.
6. Distinguish startup visuals and settings import success from network success.

### Stage D — Emergency Windows TMDB bridge / v1.1.0–v1.1.1

**Original symptom:** native PS4 HTTPS requests returned HTTP=0 and PlayTorrio synthetic **transport=-3**; settings loaded, but TMDB shows/movies failed to populate. The real cause was not yet known.

1. Introduced optional Windows PowerShell host fetcher **HostTools/Refresh-Catalog.ps1**, launched via **Load-Shows.cmd**, to call TMDB from Windows and save public metadata **catalog.json** to the shadPS4 shared data directory. Commits **9a4cc9d848**, **833dc4af2693**, etc.
2. Added local catalog parser, asynchronous local reload, TV/movie browsing in Home/Discover and pagination/selected IMDb IDs. Commits **22228b6e455**, **92538cca2b37**.
3. Mocked Windows regression tests without credential use. Published **ps4-tmdb-catalog-1.1.0**.
4. User wanted emulator portability, so added **-DataRoot** (Windows portable folder), atomic catalog writes and forwarded CMD args (**2c0c27cc4da0**).
5. Initial Windows test showed **[System.IO.File]::Replace** rejects a **null backup path**. Fixed with a .bak path and cleanup (**ccdf30b07183**). Both Windows mock tests and OpenOrbis CI then passed.
6. Published **ps4-tmdb-catalog-1.1.1** with ZIP and PKG. The bridge is no longer required for native TMDB as of the fixed emulator networking setup. Do not remove its optional functionality without instruction, but **never reinstate it as mandatory**.
7. Delivery: GitHub release ZIP and PKG verified. The separate Telegram step skipped because **TELEGRAM_BOT_TOKEN GitHub Actions secret is not configured**; user should use release links unless configured.

### Stage E — Discovery: real network support and wildcard host override (v1.2.0–v1.2.1)

**Critical confirmed root cause — DO NOT REDISCOVER AS AN API BUG:**

The user's shadPS4 Network Settings initially had **“Set Network Connected Status as True” unchecked**. Native sceHttp waited/failed; PlayTorrio used custom -3 when GetStatusCode failed, obscuring the original PS4 return.

1. On enabling network-connected status, PlayTorrio instead saw HTTP **404 / transport=0** from **TMDB, Stremio addon, TorBox user API and TorBox torrents**, each returning **exactly 389 HTML bytes**; however **example.com returned 200**. That meant HTTP connectivity worked but destination routing was suspicious.
2. Inspected shadPS4 v0.20.0 HTTP implementation: supports a user **host_overrides.json**, **wildcard "*"**, route rewriting; it can send unrelated hosts to one backend.
3. **USER CONFIRMED** their override contained **"*" mapped to localhost**. They backed it up and relaunched shadPS4. This **fixed all API routing**; TMDB, addon and TorBox returned HTTP 200. It was a real confirmed emulator configuration error, **not a fundamental inability of shadPS4 to make real HTTP(S) requests**.
4. Added precise PS4 HTTP returns and errno rather than -3, plus HTTPS control probe; enabled native asynchronous TMDB trending/popular fetch after settings loaded. Commits **cae38ecba232**, declaration fix **3117e8103320**; release **ps4-native-tmdb-1.2.0**.
5. Added deliberate public 404 control and error-body fingerprint to distinguish shared backend routing; commit **13c0df7d2217**, release **ps4-native-tmdb-1.2.1**.
6. Native TMDB now uses real console HTTP instead of Windows helper. In logs, a CATALOG entry reading **“PASS local TMDB”** is the app's historical generic log wording even when native TMDB generated the in-memory catalog. Do not assume HostTools ran on that basis.

**shadPS4 settings that WORKED:**
- Set Network Connected Status as True = checked.
- Enable shadNet = unchecked (not needed for API HTTPS).
- Enable UPnP = checked in user's screenshot, but unrelated to ordinary HTTPS requirements.
- Disable HTTPS = unchecked.
- Do not restore wildcard host override.

**Post-fix exact runtime evidence from user's sanitized diagnostics:**
- public HTTP_PROBE: **HTTP 200**, body HTML, bytes **577**.
- public intentional HTTP_404_CONTROL: **HTTP 404**, 577-byte HTML, fingerprint **0274807e4b4658cd**.
- TMDB movie trending: **HTTP 200**, JSON **13,168 bytes**, schema good.
- Stremio manifest #1: **HTTP 200**, JSON **1,391 bytes**, schema good.
- TorBox authenticated user: **HTTP 200**, JSON **828 bytes**.
- TorBox inventory initially HTTP 200, **truncated at 524,288 bytes** (bounded=0), causing diagnostic FAIL; fixed cap to **4 MiB** in v1.3.0. User then saw **HTTP 200**, JSON **823,138 bytes**, bounded=1, PASS.
- Final diagnostic suite after fix: **passed=6 failed=0 skipped=5**; skipped individual stream because **diagnosticId missing** (expected); skipped GitHub due opt-out; skipped playback until title selected.
- TMDB catalog eventually **TV=39 / movies=31** (no poster rendering confirmation).
- No external helper required for these proven native API calls.

### Stage F — Stream opens but media AvPlayer rejects URL (v1.3.0–v1.3.1)

In a user's first real playback test after network recovery:
- **RESOLVE PASS addon returned playable HTTPS stream**.
- **PLAYBACK FAIL AddSource code=-2140536830**, which is signed form of **0x806A0002**, shadPS4's **ORBIS_AVPLAYER_ERROR_OPERATION_FAILED**.
- Inspected shadPS4 v0.20.0 AvPlayer implementation. Its AvPlayerSource::Init path opens the filename through emulated PS4 mounts, not directly as a valid remote HTTP URL, so handing the signed HTTPS CDN URL directly to sceAvPlayerAddSource does not work in that emulator. v0.20.0 recognizes only H.264/HEVC/AAC streams at the codec support filter; HLS is explicitly unimplemented.
- shadPS4 AvPlayer supports **AvPlayerFileReplacement** callbacks **open/close/readOffset/size** mapped through FFmpeg AVIO custom I/O. This makes possible a console-API-based range-backed input source, without requiring Windows HostTools.
- Added HEAD Content-Length and HTTP 206 Range verification; first **64 KiB** media probe; **256 KiB** bounded range chunks; seekable callback path attached during AvPlayerInit, artificial filename **/ptvremote.mp4** passed to AddSource. Release **ps4-native-playback-1.3.0**, commit **26e761b66605**.
- Added dynamic IMDb ID lookup for user-selected later Discover pages (rather than only initial titles), and limited HTTP automatic redirect use to media HEAD/Range requests to avoid forwarding API Authorization inadvertently. Release **ps4-native-playback-1.3.1**, commit **2a2e081ab0c9**.
- Notes: for selected TV title default stream ID **tt...:1:1**; stream selection may prefer a direct HTTPS addon URL before TorBox hash resolution. Full format support remains unknown until decoder produces a frame.

### Stage G — Range-backed media accepted, but black buffering screen (last user runtime test; v1.3.1)

**Actual user screenshot:** dark 720p “PlayTorrio - PS4 hardware video” screen, controls on overlay, amber **“Buffering stream…”**, no picture visible. The complete user-provided app diagnostic log confirms:
- 1793037691: BOOT/RENDER, local settings loaded (419 bytes, one addon, TMDB set, GitHub off), native catalog fetch started.
- 1793037701–7704: **HTTPS / TMDB / ADDONS / TORBOX all PASS**, with catalog later available.
- 1793037712: first selected source attempt returned **PLAYBACK FAIL resolution did not yield a supported stream**, before the catalog was loaded; this is not the only subsequent test.
- 1793037716: catalog **PASS TV=39 movies=31**.
- 1793037720: second selection **RESOLVE PASS addon returned playable HTTPS stream**.
- 1793037722: **VIDEO_IO HEAD HTTP 200 / transport 0 / content_length present**.
- 1793037723: **VIDEO_IO PASS seekable media range, total bytes=2,429,873,904** (approximately 2.43 GB decimal).
- 1793037723: **PLAYBACK PASS AvPlayer accepted video stream**.
- 1793037723: **PLAYBACK START AvPlayer video session**.
- **No “PASS first decoded video frame rendered” was observed.** User screenshot shows buffering, so this is **NOT a successful playback test**.

Because this log ended immediately after source acceptance, it cannot by itself prove whether read callbacks were active, FFmpeg demuxing was blocked, the selected media codec was incompatible, audio was active, or the UI later timed out. The next instrumented build was created to distinguish these.

### Stage H — v1.3.2–v1.3.3: new diagnostics and codec selection; NOT YET USER-RUNTIME-TESTED

**v1.3.2 — 8e9ef2e69148; release ps4-native-playback-1.3.2**
- Identified startup timeout bug: before v1.3.2 the “no frames in 35s” failure check ran only when **sceAvPlayerIsActive returned false**. A decoder that remained “active” but yielded no frames would **buffer indefinitely**.
- Fixed timeout independent of Active; **60 seconds** without first decoded frame transitions to Failed and writes explanatory PLAYBACK line.
- Added log when AvPlayer remote-file **open** callback runs, first decoder **read** callback, first/every 32nd CDN Range request, and every ~10 seconds elapsed **BUFFERING** counters.
- Corrected manually declared AvPlayer return types to bool for **sceAvPlayerIsActive**, **sceAvPlayerGetVideoDataEx**, **sceAvPlayerGetAudioData**, matching checked shadPS4 v0.20.0 ABI.
- Added visible elapsed buffering seconds and a clearer on-screen failure message; guards invalid decoded frame dimensions and logs once.
- Portable tests and PS4 build passed. **No emulator playback result was supplied for this version.**

**v1.3.3 — a887b6c710373c5c374b6eba6d3e9b9e8c594526; release ps4-native-playback-1.3.3**
- Introduced **src/playback_choice.hpp** score and regression **tests/playback_choice_test.cpp**, run by the C++ test workflow.
- Now prefers broadly compatible streams in the addon results: H.264/AAC, 1080p and MP4 above 4K/AV1/VP9/HLS candidates; excludes unsupported stream labels. This is **heuristic metadata ranking only**, not media probing or transcoding.
- Retains the native HTTP Range file-replacement reader and 60-second stall diagnosis.
- GitHub Actions run **40375602957** succeeded: all portable tests and OpenOrbis PS4 packaging passed; release ZIP and PKG were verified via API.
- **Current next user test is v1.3.3. Do NOT report first-frame video until the user demonstrates it.**

## 5. Important architecture and debugging discoveries (do not lose)

### 5.1 HTTP transport symptom versus root cause

The old **transport=-3** was **PlayTorrio's own synthetic code** when sceHttpGetStatusCode returned negative. It was NOT itself a PS4 errno. Native network status false then wildcard "*"→localhost generated 404 responses when status was enabled. Both configuration problems have been corrected in user's emulator and native HTTPS is proven. Continue using actual status/platform_error/last_errno in logs.

### 5.2 shadPS4 v0.20.0 media path

Source locations inspected:
- https://github.com/shadps4-emu/shadPS4/blob/v.0.20.0/src/core/libraries/avplayer/avplayer_impl.cpp
- https://github.com/shadps4-emu/shadPS4/blob/v.0.20.0/src/core/libraries/avplayer/avplayer_source.cpp
- https://github.com/shadps4-emu/shadPS4/blob/v.0.20.0/src/core/libraries/avplayer/avplayer_file_streamer.cpp
- https://github.com/shadps4-emu/shadPS4/blob/v.0.20.0/src/core/libraries/avplayer/avplayer_state.cpp
- https://github.com/shadps4-emu/shadPS4/blob/v.0.20.0/src/core/libraries/avplayer/avplayer_error.h
- https://github.com/shadps4-emu/shadPS4/blob/v.0.20.0/src/core/libraries/network/http.cpp

The v0.20.0 source confirms:
- AddSourceEx rejects HLS as unsupported.
- Operation-failed AvPlayer error = **0x806A0002**.
- AvPlayerSource::Init accepts custom **file_replacement** and FFmpeg custom AVIO path; without callbacks it resolves an emulator-mounted path, not arbitrary remote CDN URL.
- FFmpeg-backed AvPlayer stream-support filter currently checks **AV_CODEC_ID_H264**, **AV_CODEC_ID_HEVC**, **AV_CODEC_ID_AAC**.
- AvPlayer autoStart is configured; adding a source is not proof that asynchronous FindStreams / decoder thread reached Play.
- GetVideoDataEx and IsActive return bool.
- AvPlayer's output audio/video still needs verified callback ABI and frame validation on physical console.

### 5.3 Unresolved media-engine risks / hypotheses

**These are hypotheses or known code limitations, not confirmed root causes:**

1. **Callback/demuxer execution**: AvPlayer may accept AddSource but never open/read the synthetic file, or the callback path could stall in shadPS4; v1.3.2+ logs now distinguish those stages.
2. **Codec, container, language streams**: addon URLs may resolve AV1/VP9/4K/MKV with incompatible video/audio, or might contain codecs that do not match description. Score-based selection helps but is not proof. Prefer actual media metadata/FFmpeg demux info where feasible.
3. **Audio selection/presentation**: current AudioOut uses 48 kHz two-channel PCM only. It silently skips different channel counts/rates; shadPS4 source may resample but still requires runtime confirmation.
4. **Rendering performance**: CPU sampling draws one 2×2 rectangle per sampled block at 720p; performance may not sustain 1080p/24fps on PS4 or emulator. GPU upload/draw is a future target, not a first-frame prerequisite.
5. **Networking inefficiency**: shadPS4's HTTP backend buffers a full **requested Range response**; current 256 KiB blocks bounded but may cause high per-chunk request overhead. Optimize only after correct frame decoding.
6. **File size / seeking**: large 2+ GB media, FFmpeg AVIO seeks, signed CDN expiry, Content-Range strictness, and custom file callback semantics can affect playback.
7. **PS4 ABI correctness**: OpenOrbis's AvPlayer headers have incomplete declarations; ps4/avplayer_abi.hpp was manually reconstructed from public homebrew/shadPS4 implementation and **real hardware behavior is not verified**.
8. **Player lifecycle**: audio thread, AvPlayer controllers, detached worker lifetime, stop/cancel, and mutexes deserve review for hangs or deadlock; preserve render-thread responsiveness.
9. **First-frame detection**: PLAYBACK PASS AddSource is merely acceptance, and Active=1 merely means the source is not marked error/stopped; only **PASS first decoded video frame rendered** demonstrates frame presentation.
10. **Source-list UI/next episode**: current flow auto-picks one candidate; no user choice/quality list, season/episode picker or next-episode autoplay. English subtitle rendering is not implemented.

### 5.4 Note about user logs and privacy

Latest runtime observations came from explicitly user-provided sanitized **diagnostics.log** files and screenshot, not live agent access to the user's PC. A future agent must ask for **new sanitized diagnostics** only if needed for a new failure. Do NOT request secrets or assume a public link identifies a media file safely.

## 6. Tests, CI, releases and distribution mechanics

### Portable tests

CI runs the exact following C++17 commands:

~~~sh
g++ -std=c++17 -Wall -Wextra -Werror src/core.cpp tests/core_test.cpp -o /tmp/playtorrio-ps4-tests
/tmp/playtorrio-ps4-tests
g++ -std=c++17 -Wall -Wextra -Werror src/core.cpp src/config_loader.cpp tests/config_loader_test.cpp -o /tmp/playtorrio-settings-loader-tests
/tmp/playtorrio-settings-loader-tests
g++ -std=c++17 -Wall -Wextra -Werror src/catalog.cpp tests/catalog_test.cpp -o /tmp/playtorrio-ps4-catalog-tests
/tmp/playtorrio-ps4-catalog-tests
g++ -std=c++17 -Wall -Wextra -Werror src/core.cpp tests/playback_choice_test.cpp -o /tmp/playtorrio-playback-choice-tests
/tmp/playtorrio-playback-choice-tests
~~~

PowerShell mock (Windows runner): **./tests/windows_catalog_test.ps1**; triggers for HostTools paths, mock test and its workflow, or manually. Does not call live TMDB or use real keys.

### Official experimental PS4 build

~~~sh
OO_PS4_TOOLCHAIN=/path/to/OpenOrbis bash scripts/build-ps4.sh
~~~

- CI: Ubuntu 24.04, clang/lld, checksum-pinned **OpenOrbis v0.5.4**, official PkgTool via isolated legacy OpenSSL 1.1 dependency, and C++17 x86_64-pc-freebsd12-elf target.
- Link libs: SceVideoOut, ScePad, SceUserService, SceSysmodule, SceFreeType, SceNet, SceSsl, SceHttp, SceAvPlayer, SceAudioOut; plus libc, libc++, libkernel.
- Builds staged app.elf, fSELF eboot.bin, sce_sys/param.sfo, icon, Gontserrat font/license, sce_module assets.
- Validates experimental PKG four-byte header **7f434e54**, and ZIP member presence and nonzero size.
- Emulator ZIP preserves **BREW20346/** path and includes both optional HostTools launchers.
- The ZIP is normally installed/extracted into shadPS4's game directory. Do not confuse it with installing PKGs through newer QtLauncher builds.
- Workflow uploads both artifacts, then creates or **--clobber** replaces assets on the fixed version tag. Docs-only main pushes also trigger current PS4 workflow; release tag is hard-coded. **Avoid mistaking republished assets for new feature code**.
- Conditional Telegram upload exists but **TELEGRAM_BOT_TOKEN** was absent in observed Actions logs. The release site, not Telegram, was the verified delivery channel. Never invent a bot token or claim sent without job log success.

### Historical release/commit index (selected)

| Stage | Release tag | Reference commit / result |
|---|---|---|
| Initial native app | ps4-prototype-0.1.0 | initial OpenOrbis executable and dashboard |
| UI previews | ps4-ui-preview-0.2.0, ps4-ui-preview-0.3.0 | controller and 720p layouts |
| Live integration | v0.4 diagnostics | native HTTPS and redacted Triangle testing |
| Library preview | v0.5 | read-only private list/caching historically; now disabled |
| Video alpha | ps4-video-alpha-0.6.0 | native AvPlayer attempt and AudioOut |
| Visual fixes | ps4-visual-fix-0.7.0 | corrected typography/focus |
| Settings / boot | v0.8/v0.9, ps4-settings-safe-1.0.0 | GitHub sync opt-out, black-screen fix and safe deferred settings |
| Windows TMDB bridge | ps4-tmdb-catalog-1.1.0 | local catalog and host fetcher |
| Portable bridge fix | ps4-tmdb-catalog-1.1.1 | ccdf30b07183; Windows + PS4 CI pass |
| Native TMDB | ps4-native-tmdb-1.2.0 | 3117e8103320; network statuses / native catalog |
| HTTPS 404 investigation | ps4-native-tmdb-1.2.1 | 13c0df7d2217; public 404 fingerprint |
| Remote file streaming | ps4-native-playback-1.3.0 | 26e761b66605; HEAD/206 callbacks |
| IMDb all pages / redirects | ps4-native-playback-1.3.1 | 2a2e081ab0c9; user saw buffering |
| Decoder stall fix | ps4-native-playback-1.3.2 | 8e9ef2e69148; CI pass, runtime not retested |
| Codec preference | **ps4-native-playback-1.3.3** | **a887b6c71037**; CI PASS, latest published, runtime not retested |

GitHub Actions run links:
- v1.1.1 final PS4: https://github.com/mghazalyh/playtorrio-ps4/actions/runs/40363063285
- v1.1.1 final Windows regression: https://github.com/mghazalyh/playtorrio-ps4/actions/runs/40363062995
- v1.2.0 last successful packaging: https://github.com/mghazalyh/playtorrio-ps4/actions/runs/40367212518
- v1.2.1: https://github.com/mghazalyh/playtorrio-ps4/actions/runs/40370369363
- v1.3.0: https://github.com/mghazalyh/playtorrio-ps4/actions/runs/40372242514
- v1.3.1: https://github.com/mghazalyh/playtorrio-ps4/actions/runs/40372887564
- v1.3.2: https://github.com/mghazalyh/playtorrio-ps4/actions/runs/40375229518
- v1.3.3: https://github.com/mghazalyh/playtorrio-ps4/actions/runs/40375602957

### Historical build failures and their lessons

- Windows PowerShell **File.Replace(temp, target, null)** rejected null backup path: use target.bak and remove it after atomic swap; Windows mock test caught it.
- Native catalog worker declaration order caused cross-compile failure; fixed prior to v1.2.0 release.
- Original transient HTTP -3 was a **synthetic client error**, not a true platform errno.
- HTTP 404 from all service endpoints was **shadPS4 wildcard routing to localhost**, not bad TMDB/TorBox/addon credentials.
- AvPlayer direct CDN URL caused **0x806A0002** because emulator expected a filesystem-backed file; file-replacement callbacks bypass that specific stage.
- Accepted remote source did **not** cause visible decoded frames in v1.3.1, so successful AddSource cannot be the final acceptance test.

## 7. Open work / prioritized next steps

### P0 — First on-screen movie frame in shadPS4

**Reproduce:** install **ps4-native-playback-1.3.3** emulator ZIP's BREW20346 folder. Keep network status enabled, HTTPS enabled, and wildcard localhost override removed. Ensure locally valid settings, TMDB and addon manifest. Launch, allow catalog to load, select a movie in Discover, press X.

Observe sanitized app log for:
1. **RESOLVE PASS preferred native video candidate** with score/source count (v1.3.3).
2. **VIDEO_IO HEAD http=200**, **PASS seekable media range** and total media bytes.
3. **VIDEO_IO PASS AvPlayer file opened** — did native file callbacks attach?
4. **VIDEO_IO PASS first decoder read callback** — did FFmpeg begin demuxing?
5. **VIDEO_IO PASS CDN range chunks=N** — were bytes fetched after initial 64 KiB?
6. **PLAYBACK BUFFERING seconds=10,20,...** and reads/HTTP counters.
7. **PLAYBACK FAIL no frames in 60s**, invalid frame info, or **PLAYBACK PASS first decoded video frame rendered**.
8. **AUDIO** events only after actual output activity.

**Diagnosis split:**
- No file-open callback after AddSource: review AvPlayer initialisation, emulator file-replacement ABI, async controller, failure events and shadPS4 AvPlayer's logger.
- Open but zero reads: review callback pointer, FFmpeg custom AVIO and state transition / hidden deadlock.
- Reads, no further Range fetch: investigate metadata/first 64 KiB, file format parsing, seeking behavior, callback errors and unsupported codec.
- Many CDN range fetches, no decoded frame: inspect actual container/codec in a privacy-safe way, stream filtering and AvPlayer demux/decoder state; consider a known authorized small H.264/AAC MP4 test stream.
- Decoder outputs frames but black UI: verify AvFrame ABI, NV12 pitch/height pointer, renderer pixel presentation and frame-timing path.
- First video frames but no sound: separate AudioOut sample format/channels/rate, stream enablement and emulator support.
- Error only after 60 seconds: the new timeout is a **diagnostic**, not a fix by itself.

Avoid moving debugging back to HostTools, TMDB key, GitHub list, VPN, or wildcard override unless new log evidence specifically implicates them.

### P1 — Robust media source and playback choices

- Test real stream codec/container compatibility rather than trusting addon labels alone; account for 4K and HEVC, and prioritize known-decodable candidate on PS4.
- Offer next candidate / explicit quality/source selection if first one fails. Current resolver auto-selects one.
- Verify safe ranged CDN semantics and expiring signed links; preserve HTTPS and bounded network memory usage. Avoid full-file downloads into RAM.
- Fix correct audio and NV12 frame presentation; move away from per-rectangle CPU video rendering toward hardware/GPU uploads when stable.
- Clean stop/cancellation and concurrent worker lifecycle.

### P2 — Product parity / final console behavior

- Actual physical GoldHEN PS4 on 10.01 installation and display/input test.
- Physical PS4 direct TMDB/Stremio/TorBox HTTPS, local settings import, native audio/video and decoder compatibility.
- English audio track selection with real audio; embedded/external subtitles with actual rendering.
- Episode/season selector, next-episode autoplay, seeking, resume watch, video overlay controls.
- Rich metadata, poster artwork and UTF-8 text display.
- Reconcile **FEATURE_PARITY.md**, **README.md**, **WINDOWS_SETUP.md** currently mixing v0.4–v1.1 older statements with newer native paths.
- Optional GitHub library sync remains **explicitly disabled** until user changes requirement.

### Definition of done for eventual real PS4 version

A genuine final version requires direct native PS4 install/use with real hardware evidence, native catalog and source lookup, sustained decoded video with correct A/V sync, supported text/audio options, responsive controls/seek, resilient buffering/error recovery, secure local configuration and authorized streams. **CI success alone does not satisfy this.**

## 8. Instructions for the next agent to resume and document changes

1. Read **AGENTS.md** and this full file; inspect current main branch HEAD and user logs.
2. Find the **last verified** version and do not conflate future docs-only HEAD with v1.3.3 feature build.
3. Complete the specific user request with source/tests, avoiding unnecessary questions or speculative marketing.
4. If code changes: run portable tests; use OpenOrbis CI to cross-compile; verify final release **assets** and job outcomes; distinguish pending runtime tests.
5. For every meaningful task: update CURRENT STATE, stage history if needed, Known Issues, Next Steps, and append an entry in CHANGE LOG below.
6. Record exact files/commits/CI runs, user observation and limitations; never put private tokens, signed media URLs, personal watchlists or raw credentials in docs.
7. Preserve all previously verified facts even when changing an implementation. Do not rewrite or erase the investigation trail.
8. When the user supplies a new screenshot/log, add sanitized outcomes and version to this file before moving to a new hypothesis.
9. If a fix requires emulator-specific behavior, explicitly document how physical PS4 behavior will be preserved or separated.
10. Do not silently switch on GitHub sync.

## 9. CHANGE LOG — append new work here (newest first)

### 2026-10-27 — Created permanent AGENTS.md and handover.md baseline (documentation only)
- **Request:** User wants the repository to preserve every future change so Codex can take over if a chat session dies. Required two root files and a comprehensive first handover.
- **Action:** Added Codex-focused engineering rules, automatic-session recovery procedure and mandatory maintenance policy; reconstructed development stages v0.1 through v1.3.3 from repository commits, source, workflow results, previous user diagnostics and screenshots.
- **Evidence:** Current main feature commit **a887b6c71037**; v1.3.3 release and GitHub Actions run **40375602957** successful; latest real emulator playback attempt was **v1.3.1** and only reached remote range/AvPlayer acceptance, no rendered frame.
- **Code change:** None. **Do not claim a decoder fix or runtime test occurred from creating these documents.**
- **Next:** Install/test v1.3.3 and use VIDEO_IO/PLAYBACK callback counts to isolate remaining black/buffering video. Keep console-first design and GitHub sync opt-out.
- **Handover creation commit:** See the most recent documentation commit in git history; this entry was seeded before commit SHA was known.

### 2026-10-27 — v1.3.3 codec preference, release; playback still unverified
- **Commit:** a887b6c710373c5c374b6eba6d3e9b9e8c594526.
- **Changed:** ps4/source_resolver.cpp, src/playback_choice.hpp, tests/playback_choice_test.cpp, .github/workflows/ps4.yml, scripts/build-ps4.sh, README.md.
- **Reason:** earlier selected addon source may have codecs shadPS4 cannot decode.
- **Implementation:** prefer H.264/AAC 1080p, penalize 4K/HDR, exclude AV1/VP9/HLS/non-HTTPS; add host regression.
- **Verified:** CI 40375602957 passed, ZIP and PKG released. **No runtime test yet.**
- **Next:** capture v1.3.3 full VIDEO_IO callback/progress and first-frame or timeout result.

### 2026-10-27 — v1.3.2 decoder I/O diagnostics and no-infinite-buffer timeout
- **Commit:** 8e9ef2e6914864c09eb00cec69d44a930315fb2f.
- **Changed:** ps4/video_player.cpp/.hpp, ps4/avplayer_abi.hpp, ps4/main.cpp, workflow/build/docs.
- **Reason:** old timeout triggered only when decoder inactive; screenshot showed indefinite buffering despite AddSource success.
- **Implementation:** bool ABI, log AvPlayer file open/read/CDN requests, 60s unconditional first-frame timeout, visible buffering seconds, invalid frame logging.
- **Verified:** CI 40375229518 passed. **No runtime decoding verified.**

### 2026-10-27 — v1.3.1 IMDb all Discover pages and safe media redirects
- **Commit:** 2a2e081ab0c92bcf7e22fe6a57e9cdd4f982983f.
- **Reason:** outside first six titles, missing IMDb ID blocked playback; avoid accidental API credential forwarding.
- **Implementation:** background IMDb lookup on title select and media-only redirects.
- **Verified:** CI 40372887564 passed; subsequent user v1.3.1 runtime observed accepted source and persistent buffering without first frame.

### 2026-10-27 — v1.3.0 remote media via PS4 AvPlayer file callbacks
- **Commit:** 26e761b66605f0744968191ed651b06f70003eab.
- **Reason:** signed AvPlayer **-2140536830** from passing direct HTTPS media URL.
- **Implementation:** HTTP HEAD/206 Range, seekable callback cache, /ptvremote.mp4 synthetic guest filename, optional 4 MiB TorBox inventory diagnostic limit.
- **Verified:** CI 40372242514 passed. **User later verified AddSource succeeds; did not verify frames.**

### 2026-10-27 — Native TMDB and network debugging, v1.2.0/1.2.1
- **Commits:** cae38ecba232c8ea9a32d4fe2b7a600df94fa37d; 3117e8103320; 13c0df7d2217584ad1de60de243ed894f7e09a21.
- **Root cause CONFIRMED:** first networking status off; then wildcard shadPS4 host_overrides.json **"*" → localhost** forced uniform HTML 404s. User backed up the override. Native TMDB/addon/TorBox HTTPS thereafter all returned HTTP 200.
- **Implementation:** corrected original PS4 error/errno capture, native TMDB async fetch, public control requests and response fingerprint.
- **Verified:** user sanitized runtime diagnostics, CI 40367212518 and 40370369363.

### 2026-10-27 — v1.1.0/1.1.1 optional Windows TMDB helper / safe filesystem updates
- **Commits:** 2c0c27cc4da0cc9477b50eb05ee095dbf59c170e; ccdf30b07183e24dcec12971edaacc209600e847, plus v1.1 upstream history.
- **Implementation:** initial HostTools catalog loader, local browsing, custom -DataRoot, File.Replace with real .bak backup, Windows mock and release validation.
- **Verified:** Windows regression 40363062995 and PS4 package 40363063285 passed.
- **Status:** HostTools is optional only; native API networking now fixed by emulator config.

### 2026-10-26 / 2026-10-27 — Native core through settings stabilization (v0.1–v1.0)
- Established separate OpenOrbis native app, portable JSON and backend logic, controller TV UI, USB settings import, AvPlayer alpha, experimental PKG and emulator ZIP, first-frame rendering fixes and safe deferred settings loader.
- Detailed source/commit/release evidence is preserved in Section 4. No physical-PS4 playback evidence was available at this handover's creation.
