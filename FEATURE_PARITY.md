# Feature parity with ptv3

`ptv3` is the behavioral reference; this project keeps independent native PS4 source.

| Feature | Core test status | PS4 device behavior |
|---|---|---|
| GoldHEN 10.01 package | Packaging pipeline added | Device installation not tested |
| Modern dark PlayTorrio TV UI | TrueType typography, cinematic hero, category rail and controls compile | PS4 screenshot/contrast verification needed |
| DualShock 4 control | Native controller API | Device testing needed |
| Import settings JSON | Core parser regression-tested | Square/X USB import implemented, hardware test pending |
| Stremio addon manifest.json | Core normalization and parser regression-tested | HTTPS transport integration pending |
| Torrentio source enumeration | Stream parser regression-tested | Addon requests and source UI pending |
| TorBox direct HTTP streams | TorBox link response parsing tested | Video playback adapter missing |
| TorBox magnet/hash debrid | NOT implemented | Needs create / enumerate / requestdl pipeline |
| Private GitHub library | JSON v1, tombstones, SHA body regression-tested | Authenticated network synchronization pending |
| English audio/subtitles | Requirement documented | Player engine not integrated |
| Next episode | Requirement documented | Player engine not integrated |
| Import from existing Flutter app | Partial JSON compatibility | Actual exported-format testing pending |
| Send PKG to existing Telegram group | Action workflow conditional on secret | Requires secret in PS4 repo |

Only mark an entry complete after tests plus real PS4 verification.
