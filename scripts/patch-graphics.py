#!/usr/bin/env python3
"""Patch the OpenOrbis sample's FreeType glyph composite.

The SDK's sample reads coverage from a *signed* char. Coverage >127 then
becomes negative, creating black/outlined letters. It also scales text
towards black instead of blending with the rendered background.

We patch only the SDK copy for this build, never modify system SDK.
"""
from pathlib import Path
import sys

if len(sys.argv) != 5:
    raise SystemExit("usage: patch-graphics.py SDK-graphics.cpp SDK-graphics.h OUTPUT.cpp OUTPUT.h")
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
# Add a video presentation API to a build-local copy of the SDK sample.
header = Path(sys.argv[2]).read_text()
needle = "void DrawPixel(int x, int y, Color color);"
if header.count(needle) != 1:
    raise SystemExit("OpenOrbis graphics.h unexpected layout")
header = header.replace(needle,
    "void BlitRgb32(const uint32_t* src,int x,int y,int w,int h);\n\t" + needle)
out += """
#include <cstring>
void Scene2D::BlitRgb32(const uint32_t* src,int x,int y,int w,int h) {
    if (!src || x<0 || y<0 || w<=0 || h<=0 ||
        x>this->width || y>this->height ||
        w>this->width-x || h>this->height-y) return;
    uint32_t* buffer = reinterpret_cast<uint32_t*>(
        this->frameBuffers[this->activeFrameBufferIdx]);
    for (int row=0;row<h;row++) {
        std::memcpy(buffer+(size_t)(y+row)*this->width+x,
                    src+(size_t)row*w,(size_t)w*sizeof(uint32_t));
    }
}
"""
Path(sys.argv[3]).write_text(out)
Path(sys.argv[4]).write_text(header)
print("Patched local graphics copies: font blending and row-wise RGB32 blit")
