# AGENTS.md — PlayTorrio PS4 agent instructions

> This file is an operational contract for Codex and every coding agent or future assistant working on this repository. **Read this and the root handover.md before changing anything.** Work is not complete until the handover is updated. This instruction applies across interrupted or replaced chat sessions.

## 1. Mission and non-negotiable priorities

**Product:** PlayTorrio, a separate **native PS4 homebrew** client intended ultimately for a GoldHEN-enabled physical PS4 (target firmware 10.01). Use the OpenOrbis SDK and PS4-native networking, controls, display, audio, and video APIs. The shadPS4 emulator is the development/test environment, **not** the primary target.

1. Design portable core logic once, usable on physical PS4 and shadPS4. Do not turn emulator-specific behavior into an unremovable hardware requirement.
2. Prefer native PS4 networking and a real console-compatible streaming architecture. Isolate emulator accommodations behind a narrow adapter only if necessary.
3. Preserve existing functional paths: TMDB catalog, Stremio addon discovery, TorBox source resolution, controller navigation, local settings, secure diagnostics.
4. **GitHub library/watchlist synchronization stays disabled by default and must not be enabled, invoked, or silently revived without the user's explicit instruction.** Do not read/write the private watchlist merely to verify a streaming change.
5. Never overclaim: a successful C++ test, PS4 cross-compile, PKG, shadPS4 launch, HTTP 200, or AvPlayer AddSource **does not prove actual decoded video, audio, subtitles, or real PS4 compatibility**.
6. Preserve private credentials and authorized-media use. Do not commit genuine user keys, manifest links containing configuration tokens, media CDN URLs, TorBox tokens, GitHub access tokens, private watchlist data, stream body bytes, or emulator HTTP logs containing full query URLs.
7. The user favors direct, practical engineering work, concise points, honest results, no unrelated marketing or speculative feature additions. Do not request information already recorded in handover.md.
8. When presenting a substantive refactor in chat, include complete changed source where practical; for repository changes, prefer links to exact commits and enumerate files.

## 2. Mandatory session startup / recovery procedure

On entering the repo, or after a chat/process/session restart:

1. Read **AGENTS.md** in full.
2. Read **handover.md** in full, including CURRENT STATE, LAST VERIFIED TEST, OPEN BLOCKERS, and CHANGE LOG.
3. Run **git status**, **git branch --show-current**, **git log -8 --oneline**, and inspect current HEAD. If using the connected GitHub plugin/API, fetch equivalent remote facts and establish the main-branch SHA.
4. Read relevant source files and tests; **source code and observed user logs outrank stale prose**. The prior handover is a starting point, not automatic proof.
5. Check the latest GitHub Actions run, the actual release assets and tag, and whether a run succeeded or failed. Do not infer a release exists from a workflow file.
6. Review the most recent user-provided emulator diagnostics and screenshots. Do not repeat solved investigations.
7. State the *precise next task and success criterion* before editing. If a new user request changes priorities, follow it and document that change.
8. If the working tree has unexpected changes, preserve them and avoid overwriting unrelated user work.

**As of the initial handover:** last feature commit a887b6c710373c5c374b6eba6d3e9b9e8c594526, release ps4-native-playback-1.3.3. All release CI passed; **no first decoded frame is verified**. Full historical context and fresh debugging instructions are in handover.md. The initial documentation-only commit that adds these files will advance HEAD; use the last feature commit to identify the tested implementation, not as an everlasting HEAD.

## 3. Required handover maintenance — EVERY meaningful task

**handover.md is a living, append-preserving project log, not a disposable summary.** Update it **in the same work unit / commit** as code changes whenever possible. Never leave the handover claiming an unverified fix is working.

For **every** implementation, debugging result, config discovery, new release, rollback, major test, architectural choice, user-requested direction change, or unresolved blocker:

- Refresh the snapshot at the top: date/time (SGT if possible), current commit/release, what actually runs, latest tests, top blocker, next step.
- Add a dated **Change Log** entry: problem, evidence, diagnosis (confirmed or hypothesis), files modified, implementation, test result, commit SHA and/or release, impact, remaining uncertainty, and follow-up.
- Update the **Verified / Not Verified** matrix when actual evidence changes. Distinguish unit tests, CI cross-compile, shadPS4 runtime, and physical PS4 tests.
- Update **Known Issues**, **Next Steps**, **Architecture / Controls / Paths** as necessary.
- Include exact diagnostic tags and sanitized error/status codes so another agent can reproduce the situation.
- Use working links to GitHub commits, runs, releases, issues; mark pending checks as **PENDING**, not PASS.
- Update AGENTS.md itself if engineering policy, safety, deployment, or handover procedure materially changes.
- Preserve a useful history; condense old entries into a stable chronological archive rather than deleting the discovery chain.
- Never paste raw secret-bearing configuration or streaming URLs into the handover, commit message, issue, PR, test fixture, or response.

For **documentation-only** tasks, record what was documented without pretending that a build changed. At the end of any turn, include enough of the handover update for a new agent to continue immediately. An interrupted coding session should leave no unrecorded assumptions.

## 4. Code layout and boundaries

| Location | Responsibility |
|---|---|
| ps4/main.cpp | PS4 entry point, screen rendering, controller tabs, startup/worker orchestration, catalog / playback UI |
| ps4/native_net.cpp and .hpp | Native sceNet/sceSsl/sceHttp, bounded HTTPS requests, response status/errno and Range headers |
| ps4/diagnostics.cpp and .hpp | FTP-readable structured redacted diagnostics; Triangle integration suite |
| ps4/catalog_online.cpp and .hpp | Native TMDB trending/popular catalog and selected-title IMDb ID resolution |
| ps4/source_resolver.cpp and .hpp | Stremio stream lookup, TorBox existing files/magnet flow, source selection |
| ps4/video_player.cpp and .hpp | AvPlayer setup, file-replacement callbacks, remote seekable range cache, decoded NV12 presentation, AudioOut |
| ps4/avplayer_abi.hpp | Carefully verified native PS4 AvPlayer ABI definitions; modify only with ABI evidence |
| src/core.cpp and .hpp | Portable settings, manifest/stream JSON parsing, TorBox and watchlist schemas |
| src/config_loader.cpp and .hpp | Safe bounded local settings file reader |
| src/catalog.cpp and .hpp | Local/shared catalog parser and TMDB results parser |
| src/playback_choice.hpp | Heuristic compatibility score for HTTPS sources |
| tests/*.cpp | Portable host C++ tests; extend whenever parsing/source policy changes |
| HostTools/* | **Optional** Windows-only legacy TMDB catalog bridge, not a real-console requirement |
| scripts/build-ps4.sh | OpenOrbis clang/lld cross-build and experimental PKG/emulator ZIP |
| .github/workflows/ps4.yml | Linux tests, real PS4 cross-compile/package, release assets, conditional Telegram |
| .github/workflows/windows-catalog-test.yml | Mocked Windows PowerShell bridge regression tests |
| .github/workflows/shadps4-smoke.yml | Historical experimental emulator smoke harness; **not** proof of current playback |
| FEATURE_PARITY.md | Parity checklist (may be stale; reconcile only when assigned) |
| README.md / WINDOWS_SETUP.md | Mixed historical documentation; some old claims are obsolete |
| handover.md | Authoritative project timeline, runtime evidence, unresolved work and takeover checkpoint |

## 5. Build and tests

Portable Linux/host regressions (same important commands as current CI):

~~~sh
g++ -std=c++17 -Wall -Wextra -Werror src/core.cpp tests/core_test.cpp -o /tmp/ptv-core-tests
/tmp/ptv-core-tests
g++ -std=c++17 -Wall -Wextra -Werror src/core.cpp src/config_loader.cpp tests/config_loader_test.cpp -o /tmp/ptv-settings-tests
/tmp/ptv-settings-tests
g++ -std=c++17 -Wall -Wextra -Werror src/catalog.cpp tests/catalog_test.cpp -o /tmp/ptv-catalog-tests
/tmp/ptv-catalog-tests
g++ -std=c++17 -Wall -Wextra -Werror src/core.cpp tests/playback_choice_test.cpp -o /tmp/ptv-choice-tests
/tmp/ptv-choice-tests
~~~

Official PS4 toolchain (not a substitute for hardware playback testing):

~~~sh
OO_PS4_TOOLCHAIN=/path/to/OpenOrbis bash scripts/build-ps4.sh
~~~

- Current CI pins **OpenOrbis v0.5.4** (checksum-verified tarball), Clang/LLD and the isolated legacy OpenSSL 1.1 needed for the official package tool.
- The output is **dist/PlayTorrio-PS4-GoldHEN-10.01-EXPERIMENTAL.pkg** and **dist/PlayTorrio-PS4-shadPS4-extracted.zip**.
- Package **TITLE_ID BREW20346**, content ID **IV0000-BREW20346_00-PLAYTORRIOPS4APP**; extracted ZIP contains **BREW20346/** plus optional **HostTools/**.
- CI checks the PKG header and that the ZIP has an eboot, param.sfo, and font; neither check proves emulator runtime compatibility.
- Current ps4.yml runs on **every push to main**, including docs-only pushes; with a fixed release tag it may upload/clobber identical-version ZIP/PKG again. Check the workflow before pushing and do not call it a new feature release unless the package content/version changed.
- Update version consistently in **scripts/build-ps4.sh** (APP_VER/VERSION) and **.github/workflows/ps4.yml** (release tag, title, notes, Telegram caption), and README/handover when releasing new code.
- Windows bridge tests are automatically filtered by paths, or can run manually; never use real credentials in those tests.
- GitHub release or Telegram delivery must be **verified separately**. The bot-token secret was missing in observed runs; Telegram skipped delivery.

## 6. Emulator setup and actual diagnostic facts

shadPS4 v0.20.0 on Windows:

- Enable **Network → Set Network Connected Status as True**.
- Keep **Disable HTTPS unchecked**. shadNet is not required for public HTTPS.
- **Do not restore the wildcard host override**: the user's former host_overrides.json had **"*" → "localhost"**, making all TMDB/Stremio/TorBox API calls return the same HTML 404. It was backed up/disabled and **native HTTPS now works**.
- Default shared PS4 path **/data/playtorrio** maps to Windows **%APPDATA%\shadPS4\data\playtorrio**; a portable emulator may use its **user\data\playtorrio** folder.
- Put real credentials only in **/data/playtorrio/settings.json**. The tracked **config/settings.example.json** is a placeholder only.
- Press **Triangle** for redacted tests; log at **/data/playtorrio/logs/diagnostics.log**, rotated to **previous.log** at the next boot.
- **Do not upload the emulator's own verbose HTTP log without redaction:** shadPS4 may print full URLs with query credentials even if PlayTorrio's app log omits them.

Key last proven runtime snapshot, from user's October 27, 2026 shadPS4 tests:
- HTTPS public probe, TMDB, addon manifest, TorBox account, TorBox list all **HTTP 200**; catalog displayed **39 TV / 31 movie records**.
- Addon resolved real **HTTPS media**, HEAD returned length, Range returned **206**, total **2,429,873,904 bytes**; **AvPlayer AddSource accepted**; UI stayed at **Buffering stream…** with **no confirmed decoded frame**.
- Newer v1.3.3 60-second timeout/callback diagnostics + source ranking were **built**, but **not yet tested by the user** in the context of this handover.

## 7. Safe engineering and release principles

- Do not bypass authentication, DRM, copyright permissions or content access controls. Only process media the user is authorized to access.
- All URLs with signed CDN query strings, Stremio addon configuration or tokens are **secrets**. Log only sanitized counters, content length, elapsed time, HTTP status, errno, codec classifications, or stable nonreversible error-body fingerprints.
- Preserve HTTPS validation and response-size limits. Do not stream an entire 2+ GB file into memory: use proper bounded HTTP byte ranges and stop/cancel handling.
- Keep PS4 UI responsive: network/catalog/source resolution runs off the render thread; be careful with lifetime, synchronization and HTTP/AvPlayer thread behavior.
- Do not introduce new Windows-only HostTools requirements for normal browsing or streaming. HostTools existed only as an emergency fallback before the host override was discovered.
- Do not modify the separate **ptv3** Flutter source or private **torriodb** repo unless the user asks. Reference them only for expected behavior/formats.
- For unknown behavior: reproduce, inspect source, add minimal diagnostics, test, observe. Clearly label hypotheses, not causes, until evidence confirms.
- A “finished playback” claim requires a **real on-screen decoded frame** plus sustained video and audible audio, and console claims require **real PS4** testing.

## 8. Current preferred next action

The user confirmed **v1.3.4 displays actual videos** but image quality looks like it is “rendered in a video game.” The new user diagnostic shows **219** and **1,059 decoded video frames**; an audio track is skipped because it is not **48k stereo PCM**. The next v1.3.5 image-quality experiment changes the native CPU scaler from blocky 2x2 output to individual bilinear HD pixels with BT.709, proper crop and limited/full range. **v1.3.5 CI passed and was released, but the user has not yet verified its picture or frame rate.** Analyze **VIDEO_QUALITY convert_ms**, output resolution and VIDEO_PROGRESS counters from the user's next emulator test; do not call playback quality fixed based only on CI.

**Crucial reference research:** Read [docs/rendering-research.md](docs/rendering-research.md) before the next graphics overhaul. Navinas3460/Stremio-PS4 uses PS4 AvPlayer, asynchronous conversion, Piglet OpenGL textures and GL_LINEAR scaling; thcolin/gamepad-media-center-aggregator uses Borealis with libmpv/FFmpeg and PS4 shader patches. Prefer a real PS4-native GPU-backed video-texture architecture over repeated CPU scaling forever; maintain separate UI/decoder concerns, full decoded image fidelity, double/triple buffering, correct color/range/crop and audio sync. Reference repo GPL-3.0 and Apache-2.0/third-party licensing must be respected; **do not paste GPL-3.0 implementation source into this repo without explicit licensing decisions.**

**Last rule:** whenever you advance this project, advance **handover.md** as part of the same task.