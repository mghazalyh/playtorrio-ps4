# Verified Windows/iOS → PlayTorrio PS4 parity audit

**Source inspection:** 2026-10-27 SGT, connected GitHub repository \`mghazalyh/ptv3\`. This is a source/feature comparison, NOT a claim of the same functionality running on PS4.

## Critical source-of-truth discovery

The **default \`main\` branch of ptv3 is NOT the active Windows/iOS application source.** Its file tree predates much of the work. The active source branch is **\`parity-floating-player-both\`**.

- Last **fully verified released** Windows and iOS version: **v1.1.7 Build 127**, source commit \`16a4143fd085973b9f103e1faa05b4ec95ad3269\` on active branch. GitHub release: https://github.com/mghazalyh/ptv3/releases/tag/build-v1.1.7-b127 ; paired CI https://github.com/mghazalyh/ptv3/actions/runs/40388302974. Windows ZIP/setup and iOS 15 IPA published, but that is not PS4 validation.
- While working, active branch progressed to **\`704ac440d3c0bd170cb2dbb28aff0f14ed1a9033\`**: \`feat(details): actor filmographies, richer similar metadata, prepare Torriodb vault (v1.1.8)\`. This is **pending CI/release** in the branch handover; do not call v1.1.8 a released version without new evidence.
- Actual ptv3 code and unit tests were read **using active branch ref**, not copied from older \`main\`. Future Codex sessions must recheck active branch and release source each time, because it is changing rapidly.
- Canonical ptv3 handover: https://github.com/mghazalyh/ptv3/blob/parity-floating-player-both/handover.md ; code refs should always include this branch. PS4 repo and ptv3 are separate and must not be merged wholesale.

## Audited feature map

| Windows/iOS capability + actual source | PS4 v1.5.0 status | Implementation / next gap |
|---|---|---|
| **Advanced Discover**: optional title + TV/movie, country of origin, original language, from/to year, sort, filtered page navigation. \`lib/services/metadata/tmdb_discover_service.dart\`, \`lib/pages/search/advanced_discover_page.dart\`; \`test/tmdb_text_filter_search_test.dart\` | **PORTED (CI-tested)** | \`src/advanced_discover.hpp\`, \`ps4/advanced_discover.cpp/.hpp\`, D-pad filter UI + virtual keyboard in \`ps4/main.cpp\`, local versioned preferences. Uses genuine \`/search\` for title and \`/discover\` when blank; local filtering for title+country/language/year/sort; movie production country triggers paced detailed lookups. PS4 filter controls use a controller rather than mobile text input. Runtime unverified. |
| **Home curation**: spotlight, hero rotation, density, ambient lights, dynamic continue-watching / BestSimilar sections, Trakt/Simkl recommendations. \`lib/services/home/home_page_settings.dart\`, \`lib/pages/home/home_page.dart\` | **PARTIAL** | PS4 Home has poster shelves for TMDB TV/movies. Glass animation / configurable sections and external-provider personalization not ported. |
| **Metadata details & episodes**: season tabs, ordered episode rows, cast, genre chips, related title cards. \`lib/pages/details/details_page.dart\`, \`lib/widgets/player/player_episodes_panel.dart\` | **PARTIAL, improved** | v1.4 TMDB episodes/posters; v1.5 adds real TMDB recommendations/similar cards with R1/L1 focus, no fake similarity percentages. Actor/cast credits not yet. |
| **Recommendations chain**: BestSimilar typed results first, optional TVDB enhancement then genuine TMDB recommendations/similar. \`lib/services/metadata/similar_recommendations_service.dart\`, \`tmdb_discover_service.dart\`, \`tvdb_series_service.dart\` | **PARTIAL** | v1.5 direct TMDB related/similar for any selected movie/show, no BestSimilar scraper and no TVDB credential dependency; no fake/movie-ambiguous types. TVDB user key is not requested on PS4 yet. |
| **v1.1.8 actor filmography**: \`lib/pages/details/person_credits_page.dart\`, TMDB \`/search/person\` and \`/person/{id}/combined_credits\` | **NOT PORTED** | Upstream branch feature **pending release**. Requires native person search/identity disambiguation, cast navigation and mixed TV/movie detail resolver. Do not search for actor names as if they are media title queries. |
| **v1.1.8 richer Related metadata**: origin country/language/network or studio, limited six-card TMDB enrichment | **NOT FULLY PORTED** | v1.5 shows real TMDB related posters/title, not yet original-language/network/studio enrichment. Never interpret original language as dubbing availability. |
| **Manual streams and sources**: addon response, episode targeting, quality labels, Torrentio/direct/TorBox, source picker. \`lib/widgets/player/player_sources_panel.dart\`, \`lib/services/stream/stream_service.dart\` | **PARTIAL, improved** | v1.4 native Stremio sources, manual selection and TorBox deferred until selected. v1.5 metadata-only 4K/1080p/720p, H264/HEVC/AV1, HDR hints; these labels are unverified, not actual container probe. PS4 still supports fewer protocols/debrid providers. |
| **Continue Watching**: save seconds/duration/episode, progress bars, resume, sync from Trakt/Simkl/optionally GitHub. \`lib/services/continue_watching/continue_watching_service.dart\`, \`lib/widgets/home/continue_watching_slider.dart\` | **NOT PORTED** | v1.5 provides **local-only My List saved titles**, no fake progress/resume claims. True progress needs native AvPlayer seek/time and durable snapshots. GitHub cloud sync explicitly disabled by user. |
| **My List library**: local/remote items with structured metadata \`lib/services/my_list/my_list_service.dart\` | **LOCAL-ONLY NEW** | PS4 stores \`/data/playtorrio/local-library.json\` containing only public TMDB metadata, atomically updated, 0600 mode, no tokens/URLs. User can save/remove via Square, open through My List tab. |
| **Player controls**: custom seek bar, aspect/speed controls, audio track menu, subtitle style/language/sync, skip-intro segments. \`lib/widgets/player/player_seek_bar.dart\`, \`player_audio_menu.dart\`, \`player_subtitle_menu.dart\`, \`player_skip_button.dart\` | **PARTIAL, urgent gap** | Native AvPlayer play/pause and 720p CPU color-converted output exist; seek/audio resampling/subtitle selection remain unfinished. Incorrect to promise full movie watching. |
| **Subtitle providers**: OpenSubtitles, SubDL, Wyzie, Stremio, SubtitleCat; extraction/parsing/styles. \`lib/services/subtitles/**\` | **NOT PORTED** | Need PS4-safe subtitle parser plus native overlay, user language choice and playback sync. |
| **More debrid providers**: TorBox, Real-Debrid, Premiumize, AllDebrid, Debrid-Link \`lib/services/debrid/providers/**\` | **TORBOX ONLY** | Preserve separate settings and auth; do not pull in other tokens without user's setup. |
| **Downloads and media formats**: HLS downloads / torrents, rich MPV playback; \`lib/services/download/**\`, \`lib/pages/player/**\` | **NOT PORTED** | Console has limited codecs and PS4-specific storage; do not pretend all Flutter/FFmpeg formats play through native AvPlayer. |
| **Anime, books, manga, audiobooks, music, IPTV**: distinct dedicated \`lib/pages/**\`, \`lib/services/**\` modules | **NOT PORTED** | Real Windows/iOS scope is broad. Each requires separate input, media engine and UX design on TV, not a renderer tweak. |
| **Simkl, Trakt**, account/library/continue watching, personalized recommendation lists | **NOT PORTED** | Require explicit OAuth/token setup and secure device storage; GitHub stays off. |
| **Advanced appearance**: configurable glass theme, dock, ambient backgrounds/hero, density, posters \`lib/services/theme/**\`, \`lib/pages/settings/appearance/**\` | **PARTIAL** | PS4 now has native poster shelves, details and keyboard. Piglet/GPU image & video surfaces remain the architecture priority. |
| **v1.1.8 Torriodb key vault plan** \`docs/torriodb_key_vault_plan.md\` | **NOT PORTED BY DESIGN** | Upstream documents a **proposal**, NOT a completed secure vault. Never store user API credentials in public GitHub, sync private credentials without user consent, or reactivate GitHub syncing. |

## Important TMDB semantics preserved from real ptv3 v1.1.7

- **Blank title:** TMDB \`/discover/movie\` or \`/discover/tv\` with \`sort_by\`, \`vote_count.gte=100\` (top-rated), production country, original language, movie primary-release date or TV **air_date** window.
- **Nonempty title:** TMDB \`/search/movie\` or \`/search/tv\` with \`query\`; cannot send Discover-only options to Search. Locally filter search pages for country, language, year and sort. Movie production-country verification may require one \`/movie/{id}\` lookup per otherwise qualifying Search result; extra lookups are bounded and paced.
- **Single-year TV search** lets TMDB search year include any episode aired in that year; **multi-year text TV search** locally checks *premiere* year, not every possible episode-airing date. UI should explain this difference.
- **Search page totals** are raw TMDB page totals; a filtered page might contain no matching titles while later pages have matches. Never label server-wide counts as filtered matches.

## PS4 v1.5.0 files / behavior

- \`src/advanced_discover.hpp\`, \`ps4/advanced_discover.cpp/.hpp\`, \`tests/advanced_discover_test.cpp\`: filter model, sanitized TMDB paths, client-side search filter + sort, safely serialized local preferences, native rate-bounded detail requests.
- \`ps4/catalog_online.cpp/.hpp\`: real TMDB \`/tv|movie/{id}/recommendations\`, optional \`/similar\` fallback, deduplication, no false "similarity percentage".
- \`src/local_library.hpp\`, \`tests/local_library_test.cpp\`: only public TMDB metadata; saves \`/data/playtorrio/local-library.json\`; no cloud calls, stream URLs or progress estimates.
- \`src/source_badges.hpp\`, \`tests/source_badges_test.cpp\`: conservatively classify *untrusted addon labels* as possible resolution, codec/HDR; not codec verification.
- \`ps4/main.cpp\`: Advanced Filters on Discover **Square**; D-pad keyboard for optional title, 1900–2100 year adjustment, country/language/sort, Apply/Clear; paged native TMDB results; Related on title details **R1**, back to episodes **L1**; **Square** saves/removes title locally; My List tab uses local saved titles and does **not** reach GitHub when disabled. \`/data/playtorrio/discover-filters.json\` keeps filter UI choices on the device only.
- \`scripts/build-ps4.sh\`, \`.github/workflows/ps4.yml\`: compile new discover module, run new regression tests, release as \`ps4-advanced-discover-1.5.0\`.

## Platform constraints / next priorities

1. **First user shadPS4 acceptance** of v1.4/v1.5 poster and episode flow, Advanced Discover navigation and local My List; C++/PkgTool passing does not prove runtime.
2. Native GPU/Piglet compositor and async video conversion, correct frame cadence and audio tracks, not Windows-only helper playback.
3. Audio PCM resampling/downmix, language track picking, embedded/subtitle provider overlays, seeking/resuming and progress.
4. Cast/actor filmographies, genuine film metadata fields/countries/network source, TVDB opt-in, genre/cast screens.
5. Optional provider integrations (Trakt/Simkl), home personalization with **explicit token onboarding**, privacy-preserving local storage.
6. Physical PS4 GoldHEN firmware 10.01 test of install, networking, display and video; never infer from shadPS4.

**Do not copy Dart Flutter UI source directly into C++/OpenOrbis**, copy behavior and semantics. Do not copy GPLv3 Stremio-PS4 application source without licensing decision. This parity document is grounded in inspected code; feature support claims require independent PS4 runtime evidence.
