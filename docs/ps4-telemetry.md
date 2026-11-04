# PlayTorrio PS4 private playback telemetry

PS4 uses the existing private `mghazalyh/torriodb` repository for diagnostics. The PS4 app submits directly to the GitHub Contents API, so normal player debugging does not require manually transferring FTP logs.

## Enable on the console or shadPS4

Add these keys to your EXISTING `/data/playtorrio/settings.json` without removing TMDB/TorBox/addon settings:

```json
{
  "githubSyncEnabled": false,
  "telemetryEnabled": true,
  "telemetryToken": "YOUR_PRIVATE_GITHUB_CONTENTS_WRITE_TOKEN"
}
```

The token needs Contents read/write permission on the private `mghazalyh/torriodb` repository. Keep it in the local settings file, never in code or Git. `githubToken` remains a fallback only when `telemetryEnabled` is explicitly enabled. Watchlist/library sync is independent and stays disabled.

## Private destination and samples

`playtorrio/diagnostics/ps4/player/YYYY/MM/DD/<UTC-timestamp>-<ephemeral-sequence>.json`

Reports follow TorriODB's UTC date-partitioned Windows/iOS archive. Each report includes public build commit/run provenance, UTC timestamp, up to 24 fixed-code events, a numeric playback snapshot and up to 24 rolling five-second numeric performance samples.

The samples expose decoder/output dimensions, FPS, frame deltas, NV12 conversion and blit costs, frame gaps/stalls, HTTP range fetch and decoder read counts, audio output counts, audio channel count/sample rate and unsupported-format indicator. Buffering and decoder timeouts also publish zero-frame metrics.

Uploads happen automatically after startup, key player events, first frame, progress, stop and failures. Failed uploads retry while the app runs; attempts are throttled to one every 30 seconds. Local sanitized `/data/playtorrio/logs/diagnostics.log` reports `TELEMETRY PASS ... http=201` only when a private report was accepted.

## Privacy and limitations

Telemetry remains opt-in. Reports contain no media titles, IMDb/TMDB IDs, signed stream URLs, tokens, torrent hashes, subtitle text, raw media, console serials or stable device fingerprints. A hard crash/power-off before successful upload can lose bounded in-memory events. CI success cannot prove an HTTP 201 from shadPS4/PS4.

The PR-only workflow sends the exact GitHub Actions run/artifact URL to Telegram whether test/package jobs pass or fail. It requires the **PS4 repository** Actions secret `TELEGRAM_BOT_TOKEN`. Optional `TELEGRAM_CHANNEL_ID` overrides the historical PlayTorrio destination. Missing credentials fail the notification job visibly instead of silently claiming delivery. PR builds cannot overwrite the public v1.5.0 release.

## November 4: Extended PS4 playback flight-recorder fields

The active PS4 backend is `avplayer_cpu_sse2_worker_videoout`, **not** FFmpeg/libmpv or a Piglet texture renderer. Each opt-in private TorriODB report now identifies that backend, compile-time build commit/build run, a boot-local ephemeralSession nonce and the UTC time.

New bounded numeric fields, both in the current `playback` snapshot and up to 24 rolling five-second `performanceSamples`, include:
- `presented_fps_x100`, `video_presented_frames`, `video_skipped_frames`, `video_cached_redraws`: presentation throughput, dropped intermediate frames, expected repeated display and decoder-to-presenter differences.
- `video_worker_enabled`, `video_empty_polls`, `video_invalid_frames`, `video_conversion_failures`: decoder/worker state and conversion failure counts.
- `audio_empty_polls`, `audio_converted_chunks`, `audio_output_failures`, `audio_invalid_chunks`, `audio_buffers`, `audio_channels`, `audio_sample_rate_hz`: format compatibility, native audio drain and output failures.
- `range_downloaded_bytes`, `range_retry_attempts`, `range_read_timeouts`, `range_rejected_blocks`, `range_requests`, `decoder_read_calls`: CDN/HTTP byte-range health without URLs or content.

The recorder now receives a fresh snapshot every five seconds while the video screen renders; even quiet/buffering sessions schedule private reports, with the existing **30-second minimum between upload attempts**. The max history remains 24 events and 24 samples in RAM and retries remain bounded. To actually send telemetry from PS4, `telemetryEnabled: true` and a locally configured `telemetryToken` with Contents write access to private TorriODB are required. Disabling telemetry discards stored data. Crash/power loss before upload can still lose data.

The source contains *no* Telegram bot token. The PS4 repository `TELEGRAM_BOT_TOKEN` GitHub Actions secret must be configured for the PR-only sendMessage notification; missing secrets cause a notification failure but do not invalidate a successful native package job.

Compilation alone cannot validate audio, real hardware video output, HTTP 201 or Telegram delivery.
