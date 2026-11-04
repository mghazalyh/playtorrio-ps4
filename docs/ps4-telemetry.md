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
