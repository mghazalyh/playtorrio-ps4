# v1.4.0 PS4 UI integration — ptv3 and native console references

Date: 2026-10-27 SGT. Main product target: GoldHEN physical PS4, not a Windows emulator app.

## Reviewed source repositories

### Windows / iOS — user's Flutter app

Source: https://github.com/mghazalyh/ptv3

- \`lib/pages/details/details_page.dart\`: metadata detail page, fullscreen background/poster, title, cast/similar, season selector, sorted episodes, and responsive layouts. Takes metadata from the app's metadata service; not a PS4 library.
- \`lib/widgets/player/player_episodes_panel.dart\`: episode focus grouped by season, current episode tracking, scroll navigation, grouped batches for long-running series, tapping chosen episode opens source selection.
- \`lib/widgets/player/player_sources_panel.dart\`: show/episode-specific Stremio/other source enumeration, progressive retrieval, source cards and play-selected action. It does not play the first source without user selection.
- \`lib/services/stream/stream_service.dart\`, \`lib/models/stream/stream_model.dart\`: stream providers and source metadata differ between hosts; use portable Stremio protocol/IDs instead of copying Dart source.

**Ported conceptually to native PS4:** title details, per-season episode browsing, season-specific \`ttIMDb:season:episode\` identifiers, source enumeration and manual source choice. NOT ported yet: rich poster backdrops, providers beyond installed Stremio addons, cast/similar results, fine-grained quality filter chips, subtitles, user progress and watchlist sync.

### PS4 reference Stremio

Source: https://github.com/Navinas3460/Stremio-PS4

- \`src/net/images.cpp/.h\`: HTTPS artwork downloads on background workers; \`stb_image\` JPEG/PNG decoder (plus FFmpeg WebP); image size safety checks, downscaling, deferred GPU uploads, cache with least-recently-used eviction. Explicitly excludes huge images and avoids decode/upload on the UI thread.
- \`src/ui/details_screen.cpp\`: detail metadata, art/landscape image fallbacks, sorted episode selection, per-episode source retrieval and stream choice.
- \`src/render/renderer.cpp\`: GPU textures and bilinear filtering; console-first rendering guidance in \`docs/rendering-research.md\`.

**Adopted concepts only:** native PS4 asynchronous artwork fetch and bounded cache, selected episode/stream screens. Vendored **upstream stb_image.h v2.25** from the reference's third_party/stb copy; stb_image itself is public-domain / MIT dual offer per upstream file, not GPL code. Do **not** copy original reference GPL application code without license review.

### GMCA

Source: https://github.com/thcolin/gamepad-media-center-aggregator

- \`app/src/tab/media_series.cpp\`, \`app/src/utils/image_cache.cpp\`, \`app/src/view/video_source.cpp\`: structured TV metadata, asynchronous image caches, playback source lists.
- \`scripts/ps4/libmpv/PKGBUILD\`: OpenOrbis-ported libmpv, but a significantly larger runtime + shader integration than the current PS4-native client.

**Not copied:** libmpv/FFmpeg/Borealis. Code remains PS4-native OpenOrbis; a separate GPU renderer milestone is still appropriate.

## Implemented in v1.4.0

- \`src/catalog.hpp/.cpp\`: TMDB \`poster_path\`, \`backdrop_path\` (and optional local catalog JSON fields) retained.
- \`vendor/stb_image.h\`: image library with its own permissive/public-domain license.
- \`ps4/poster_cache.hpp/.cpp\`: validates incoming TMDB relative image paths; makes HTTPS requests to \`image.tmdb.org/t/p/w342\`; bounds download at 512 KiB, compressed image dimensions at 2048×2048 / 1.8 megapixels; downsizes to 160×240 RGB32. Asynchronous two-worker limit, 28-entry LRU with bounded memory. The native Scene2D VideoOut blitter shows images while UI stays responsive.
- \`src/show_details.hpp\`: portable parser of TMDB seasons and episode lists. Rejects invalid season/episode numbers; sorts by season/episode; generates validated Stremio episode identifiers \`ttNNNN:s:e\`.
- \`ps4/catalog_online.cpp/.hpp\`: native PS4 authenticated TMDB requests for \`/tv/{id}\` and \`/tv/{id}/season/{season}\`; settings token is never logged.
- \`ps4/source_resolver.cpp/.hpp\`: \`list_sources()\` fetches installed addon results without TorBox side effects, keeps up to 100 candidates and ranks direct streams first. \`resolve_selected()\` resolves only the selected candidate, including optional TorBox hash handling. Original automatic \`resolve()\` remains for legacy diagnostics and settings source tests.
- \`ps4/main.cpp\`: large native TV-controller UI improvement with artwork in Home/Discover, selected metadata, details panel, L2/R2 season change, sorted episode rows, dedicated source picker and deferred selected-source playback. Circle backs through sources→details→browse; D-pad navigation; R2/L2 source paging.
- \`tests/show_details_test.cpp\`: portable JSON/episode-ID regression cases.
- \`scripts/build-ps4.sh\` and \`.github/workflows/ps4.yml\`: added JPEG/PNG decoder cross-compilation, linked new poster cache and added unit test; title APP_VER 01.40; release \`ps4-cinematic-ui-1.4.0\`.

## What is tested / still unverified

- CI C++ core/settings/catalog/NV12/episode tests: **PASSED**, v1.4.0 workflow 40386525983.
- PS4 Clang/LLD cross-compile, PKG signature validation and shadPS4 ZIP contents: **PASSED**, workflow 40386525983.
- Native TMDB HTTPS and Stremio/TorBox networking: **previously verified** on user's shadPS4 v1.3.4.
- New poster image decoding, season switching, source picker and visual controller flow on an actual shadPS4 or physical PS4: **NOT YET VERIFIED**. CI doesn't verify button timing, CDN artwork decode or modal usability.
- Native video picture quality and audio: **unfinished**. v1.3.5 bilinear CPU output likewise unverified by user.
- GitHub library/watchlist sync: **stays disabled**. Never silently reactivate.

## Next engineering milestones

1. User runs v1.4.0. Validate poster cards become actual artwork, selecting a show loads seasons and episodes, a selected episode loads sources, manual selection starts the intended stream. Record sanitized \`DETAIL\`, \`SOURCES\`, \`CONTROL\`, \`VIDEO_IO\` logs.
2. Fix responsive UI and poster cache behavior observed in shadPS4 (download errors, load latencies, image decoding) without adding Windows host dependencies.
3. Build real GPU Piglet textures to replace the CPU VideoOut pixel blitter and support high-quality artwork/backdrop gradients and smoother video.
4. Add full title metadata/cast/related series and a better source information panel (size/quality/language/cache status) inspired by ptv3, with privacy limits.
5. Persist **local-only** watch/resume status if wanted, separate from disabled GitHub watchlist.
6. Audio resampling/channel support, subtitles, seek controls and hardware-console confirmation remain major playback tasks.

Data hygiene: never store stream URLs/signed links, addon auth tokens, TorBox tokens, genuine TMDB keys or private user watchlists in repo/tests/logs. Avoid copying GPLv3 code wholesale; stb_image is independently permissively licensed.
