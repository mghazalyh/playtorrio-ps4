#!/usr/bin/env python3
"""Patch the OpenOrbis sample's FreeType glyph composite.

The SDK's sample reads coverage from a *signed* char. Coverage >127 then
becomes negative, creating black/outlined letters. It also scales text
towards black instead of blending with the rendered background.

We patch only the SDK copy for this build, never modify system SDK.
"""
from pathlib import Path
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: patch-graphics.py SDK-graphics.cpp OUTPUT.cpp")
src = Path(sys.argv[1]).read_text()
start_marker = "                // Decode the 8-bit bitmap"
end_marker = "            }\n        }\n\n        // Increment x offset"
start = src.find(start_marker)
end = src.find(end_marker, start)
if start < 0 or end < 0 or src.count(start_marker) != 1:
    raise SystemExit("OpenOrbis graphics source has changed; refusing unsafe patch")
end += len("            }\n        }")
fix = r"""                // FreeType's grayscale coverage is UNSIGNED. The original
                // sample stores it in a signed char: pixels > 127 turn negative.
                // This produced jagged, dark/outlined text in shadPS4.
                const uint8_t coverage =
                    static_cast<uint8_t>(slot->bitmap.buffer[
                        yPos * slot->bitmap.pitch + xPos]);

                const int x = startX + xPos + xOffset + slot->bitmap_left;
                const int y = startY + yPos + yOffset - slot->bitmap_top;
                if (coverage == 0 || x < 0 || y < 0 ||
                    x >= this->width || y >= this->height)
                    continue;

                // Do NOT read back from the PS4 video-out mapped buffer.
                // Scene2D writes directly into video memory. Several drivers
                // (and shadPS4) permit writes but cannot safely read it back.
                // Preblend coverage with the caller-provided background,
                // then use the proven DrawPixel write-only operation.
                const unsigned alpha = coverage;
                const unsigned inv = 255u - alpha;
                const uint8_t rr = static_cast<uint8_t>(
                    (alpha * fgColor.r + inv * bgColor.r + 127) / 255);
                const uint8_t gg = static_cast<uint8_t>(
                    (alpha * fgColor.g + inv * bgColor.g + 127) / 255);
                const uint8_t bb = static_cast<uint8_t>(
                    (alpha * fgColor.b + inv * bgColor.b + 127) / 255);
                this->DrawPixel(x, y, Color{rr, gg, bb});
            }
        }"""
out = src[:start] + fix + src[end:]
if "const uint8_t coverage" not in out or out.count("this->DrawPixel(x, y, Color{rr, gg, bb});") != 1:
    raise SystemExit("Anti-alias fix failed to apply")
Path(sys.argv[2]).write_text(out)
print("Patched OpenOrbis FreeType: unsigned coverage + write-only background blending")
