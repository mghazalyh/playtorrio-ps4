# Feature parity: active Windows/iOS \`ptv3\` versus native PS4

**Canonical source audit:** [docs/ptv3-active-feature-audit.md](docs/ptv3-active-feature-audit.md). \`ptv3\` application development occurs on branch \`parity-floating-player-both\`, **not** default \`main\`. Latest independently verified Windows/iOS release at audit: **v1.1.7 Build 127**. Newer v1.1.8 cast-filmography work was in progress, not released. Recheck for new commits before any next comparison.

| Feature | Native PlayTorrio PS4 status | What remains |
|---|---|---|
| TMDB trending/popular catalog | CI/build + prior shadPS4 API tested | Real console verify |
| TMDB JPEG/PNG poster cache | Implemented v1.4 | User runtime graphics test |
| Selected TV seasons and episodes | Implemented v1.4 | User controller/metadata test |
| Stremio addon source picker | Implemented v1.4 | Actual selected-stream runtime check |
| v1.1.7 Advanced Discover combined title/filters | **Implemented v1.5**, tests pass | UI/live result pagination acceptance |
| Search query filtering: year, country, language, sort | **Implemented v1.5**, tests pass | Rate limiting, on-screen correctness |
| Local save/remove My List | **Implemented v1.5**, tests pass | Local file read/write in emulator/console |
| TMDB recommended and similar movie/TV | **Implemented v1.5** | Correct visible related row and navigation |
| Source quality/codec/HDR hints | **Implemented v1.5**, tests pass | Hints are not codec verification |
| Native video AvPlayer and HTTPS range cache | Previous runtime decoded frames | Smooth/full quality, audio formats, console |
| Actual watch-progress resume and continue watching | **Not implemented** | AvPlayer timestamps/seek, persistence, player UI |
| BestSimilar/TVDB recommendation enrichment | **Not implemented** | Provider APIs/credentials, metadata verification |
| Actor credits and filmography (ptv3 v1.1.8) | **Not implemented** | Actual person search, disambiguation, credits |
| Audio language/downmix/subtitle rendering | **Not complete** | Native audio pipeline + subtitles |
| GPU Piglet video/artwork compositor | **Not implemented** | Real PS4 GPU pipeline and performance checks |
| Trakt / Simkl / music / IPTV / anime / books | **Not implemented** | Independent UI/providers/platform engines |
| GitHub watchlist and Torriodb key vault | **OFF intentionally** | User must explicitly authorize any sync |
| Real GoldHEN PS4 firmware 10.01 test | **Not tested** | Actual install, controller, video/sound |

**Latest PS4 package:** \`ps4-advanced-discover-1.5.0\`. Successful native CI builds are NOT proof of full UI/playback; see handover. Previous v0.4–v1.3 histories retained in \`handover.md\`.
