# Feature parity with ptv3

`ptv3` is the behavioral reference; this project keeps independent native PS4 source.

| Feature | Core test status | PS4 device behavior |
|---|---|---|
| GoldHEN 10.01 package | Packaging pipeline added | Device installation not tested |
| Modern dark PlayTorrio TV UI | TrueType typography, compact 720p hero, 16 focusable home tiles, six settings rows compile | PS4 screenshot/contrast verification needed |
| DualShock 4 control | Native controller API | Device testing needed |
| Import settings JSON | Core parser regression-tested | Square/X USB import implemented, hardware test pending |
| Stremio addon manifest.json | Core tests pass | Native HTTPS diagnostic test implemented; console verification pending |
| Torrentio source enumeration | Core parsing passes | Native live stream lookup by configured diagnosticId implemented; full source UI pending |
| TorBox direct HTTP streams | Response parsing passes | Native account/list/file/CDN diagnostic implemented; playback adapter missing |
| TorBox magnet/hash debrid | NOT implemented | Needs create / enumerate / requestdl pipeline |
| Private GitHub library | JSON v1, tombstones, SHA body regression-tested | Authenticated GET and identical SHA-guarded PUT diagnostic implemented; continuous sync pending |
| English audio/subtitles | Requirement documented | Player engine not integrated |
| Next episode | Requirement documented | Player engine not integrated |
| Import from existing Flutter app | Partial JSON compatibility | Actual exported-format testing pending |
| Send PKG to existing Telegram group | Action workflow conditional on secret | Requires secret in PS4 repo |

Only mark an entry complete after tests plus real PS4 verification.

### Integration log and user verification

The v0.4 GoldHEN build writes `/data/playtorrio/logs/diagnostics.log`. Press Triangle from any tab to run the real HTTP integration checks and retrieve the file over FTP. No one has verified these API operations on a physical PS4 yet. Do not call tests passing in CI equivalent to console streaming success.

Native PlayTorrio PS4 v0.5 includes a read-only list of titles fetched from private GitHub storage after Triangle. Video playback and subtitle selection are still not implemented.

## v0.6 hardware playback alpha

- Built and linked real `libSceAvPlayer` video decoding, native NV12 frame presentation, and PS4 audio-output calls. PS4 console playback and audio need actual hardware verification.
- Source resolution now attempts direct HTTPS video, existing TorBox torrent file links, configured Stremio addon direct streams, and Torrentio infoHash → TorBox create/lookup → CDN links.
- Controller can request playback from Discover/Addons and a fetched My List title, then pause/resume/exit a session.
- No fabricated playback claims: audio language switching, subtitles, next episode, GPU rendering performance, source UI, and full parity remain open.
