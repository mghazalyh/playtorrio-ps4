# PS4 rendering research — two open-source player references

**Date:** 2026-10-27 (SGT)  
**Target:** physical GoldHEN PS4 first, shadPS4 development second.  
**Our baseline:** PlayTorrio v1.3.4 could show decoded frames, but they appeared blocky, visibly unlike a real movie player; current audio PCM restrictions are separate from visual quality.

## Studied reference 1: Navinas3460/Stremio-PS4

Repository: https://github.com/Navinas3460/Stremio-PS4  
License: **GPL-3.0**. We studied publicly visible implementation concepts only; do **not** copy implementation into PlayTorrio without intentional compatible licensing and attribution decisions.

### PS4 rendering and decoding evidence

- `src/video/video_avplayer.cpp`: hardware AvPlayer, file replacement callbacks for remote media, decoder frames in NV12, AudioOut pacing on a dedicated thread, independent video conversion worker and multiple buffers. Its author reports physical PS4 FW 11.02 testing. App invokes FFmpeg as optional/default alternative, not merely a GUI over AvPlayer.
- `src/video/nv12.h`: decoder stride alignment, visible frame crop, BT.709 for HD versus BT.601 for SD, limited/full YUV range, and SSE2 fixed-point conversion. The author reports optimized 1080p NV12 conversion on PS4.
- `src/render/renderer.cpp`: converts RGBA bytes into OpenGL textures, uses `GL_LINEAR` for min/mag texture filtering, updates via `glTexSubImage2D` without recreating the texture each frame, and composites through batched triangle draw calls.
- `src/platform/platform_ps4.cpp`: Piglet/OpenGL ES 2 initialization, precompiled swordfish shaders, explicit graphics memory reservations small enough to leave direct memory for the hardware decoder, and library loading for GnmDriver.
- **Important real-PS4 detail:** AvPlayer video texture allocations require GPU-visible direct memory and proper aligned height. Converting on a separate CPU worker prevents display/input stalls.
- If implementing the long-term GPU backend, preserve a simple abstract video-texture interface, consistent full-frame color handling, buffered producer/consumer queue and emergency VideoOut fallback. The entire 2D UI may need migration to Piglet if the renderers cannot share the same surface.

## Studied reference 2: thcolin/gamepad-media-center-aggregator (GMCA)

Repository: https://github.com/thcolin/gamepad-media-center-aggregator  
License: **Apache-2.0** for the project, but bundled MPV/FFmpeg and other deps have their own licenses.

### PS4 and cross-platform rendering evidence

- `README.md`: video playback via MPV, with broad codec/container/audio/subtitle handling and transcode/HLS support.
- `CMakeLists.txt`: platform-specific builds, options for MPV software rendering (not ideal for sustained PS4 use), and separate PS4 integrations.
- `scripts/ps4/libmpv/PKGBUILD`: an OpenOrbis port of **libmpv 0.36** linked with SDL2/FFmpeg.
- `scripts/ps4/libmpv/mpv.patch`: replaces runtime GLSL compilation with PS4 precompiled shader selection, necessary for the PS4 Piglet GL path.
- GMCA's full MPV/Borealis stack is a substantially **larger** dependency and integration commitment than PlayTorrio's current native AvPlayer implementation. Its repo having PS4 source does not alone prove the same media streams or codecs will run well on the user's exact PS4 firmware.

## Why v1.3.4 looks like a game / what changes now

Previous PlayTorrio renderer (`ps4/video_player.cpp`):
- Takes NV12 and displays at most **960×540** into a **1280×720** video output.
- Samples one pixel per **2×2 block**, directly repeating its RGB value into four pixels. This is nearest-neighbor-style pixelation, not smooth scaling.
- Uses SD BT.601 fixed coefficients even on 1080p video and ignores AvPlayer crop/full-range metadata.
- Performs all frame conversion in the main UI frame loop; heavy work reduces effective frame rate.
- Audio log from the v1.3.4 user test: **SKIP stream format not 48k stereo PCM** for one video; this is an independent sound blocker.
- User's v1.3.4 test log: first show **219 frames**, second **1,059 frames**; first-frame decode and native CDN byte-range handling work.

### v1.3.5 first corrective step (original implementation, not copied)

- Add `src/nv12_scaler.hpp` with normalized crop/pitch/visible size handling and bilinear luma **and** chroma resampling per destination pixel.
- Use proper BT.709 HD conversion and full/limited range flag; preserve BT.601 for SD. Use full `1280×720` output envelope and aspect-preserving letterbox.
- Add `tests/nv12_scaler_test.cpp` for edge cases, gradients, limited brightness, padding and invalid data.
- Continue using native PS4 VideoOut, no Windows host renderer. Add `VIDEO_QUALITY` log with output dimensions, HD matrix choice, and conversion time.
- **Status:** C++ tests and PS4 cross-compile can validate correctness; visual smoothness and CPU cost must be tested in shadPS4 and eventually on physical PS4. This upgrade may be sharper but run **slower** than the block renderer.

## Next architectural milestone

1. Build a **PS4-native Piglet/EGL video texture renderer** that can upload a full decoded frame and let the GPU handle bilinear scaling through `GL_LINEAR`, preferably with a compositor for UI overlays.
2. Move YUV→RGBA conversion off the UI thread, with double/triple buffers, explicit decoder memory allocation and timestamp-based frame pacing.
3. Keep the current native AvPlayer/HTTP Range source reader initially, so we change one variable at a time. Use libmpv/FFmpeg only if AvPlayer lacks the required codecs/audio/subtitle support; don't drop GMCA's full stack directly into the small PKG without cost and license analysis.
4. Investigate audio sample-rate/channels/format conversion with a proper paced queue and subtitles separately.
5. Benchmark real PS4 and shadPS4 **visible frame rate and audiovisual synchronization**, not just decoded-frame counts.
6. Keep `githubSyncEnabled=false` until explicit user request.

Source reading is architectural analysis and cannot substitute for runtime testing or firmware-specific compatibility.
