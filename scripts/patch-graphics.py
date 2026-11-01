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
    "void BlitRgb32(const uint32_t* src,int x,int y,int w,int h);\n\t"
    "void BlitVideoRgb32(const uint32_t* src,int x,int y,int w,int h);\n\t" + needle)
# Keep the existing 1280x720 UI layout but emit actual 1920x1080 video.
# DrawPixel is the shared UI primitive, so map each logical pixel to 1 or 2
# physical scanout pixels without affecting the full-resolution video blitter.
draw_pixel = "int pixel = (y * this->width) + x;"
if out.count(draw_pixel) != 1:
    raise SystemExit("Unexpected OpenOrbis DrawPixel implementation")
out = out.replace(draw_pixel, """if (this->width == 1920 && this->height == 1080) {
        if (x < 0 || y < 0 || x >= 1280 || y >= 720) return;
        const int left = x * 3 / 2, right = (x + 1) * 3 / 2;
        const int top = y * 3 / 2, bottom = (y + 1) * 3 / 2;
        const uint32_t c = 0x80000000u | (uint32_t(color.r) << 16)
                           | (uint32_t(color.g) << 8) | color.b;
        uint32_t* p = reinterpret_cast<uint32_t*>(
            this->frameBuffers[this->activeFrameBufferIdx]);
        for (int yy = top; yy < bottom; yy++)
            for (int xx = left; xx < right; xx++)
                p[(size_t)yy * this->width + xx] = c;
        return;
    }
    """ + draw_pixel, 1)
fill = "DrawRectangle(0, 0, this->width, this->height, color);"
if out.count(fill) != 1:
    raise SystemExit("Unexpected OpenOrbis FrameBufferFill implementation")
out = out.replace(fill, r"""if (this->width == 1920 && this->height == 1080) {
        // Fast scanout clear. Avoid 1280x720 DrawPixel calls and per-pixel
        // coordinate scaling on every playback frame.
        uint32_t* pixels = reinterpret_cast<uint32_t*>(
            this->frameBuffers[this->activeFrameBufferIdx]);
        const uint32_t packed = 0x80000000u | (uint32_t(color.r) << 16)
                                | (uint32_t(color.g) << 8) | color.b;
        const size_t count = (size_t)this->width * this->height;
        for (size_t i = 0; i < count; ++i) pixels[i] = packed;
    } else {
        DrawRectangle(0, 0, this->width, this->height, color);
    }""", 1)
out += """
#include <cstring>
// Bitmap and poster draws use the same 720p logical coordinate system as UI.
void Scene2D::BlitRgb32(const uint32_t* src,int x,int y,int w,int h) {
    if (!src||x<0||y<0||w<=0||h<=0) return;
    uint32_t* target=reinterpret_cast<uint32_t*>(
        this->frameBuffers[this->activeFrameBufferIdx]);
    if (this->width == 1920 && this->height == 1080) {
        if (x >= 1280 || y >= 720 || w > 1280-x || h > 720-y) return;
        for (int row=0;row<h;row++) {
            const int top=(y+row)*3/2, bottom=(y+row+1)*3/2;
            for (int col=0;col<w;col++) {
                const int left=(x+col)*3/2,right=(x+col+1)*3/2;
                const uint32_t pixel=src[(size_t)row*w+col];
                for (int yy=top;yy<bottom;yy++)
                    for (int xx=left;xx<right;xx++)
                        target[(size_t)yy*this->width+xx]=pixel;
            }
        }
        return;
    }
    if (x > this->width || y > this->height ||
        w > this->width-x || h > this->height-y) return;
    for (int row=0;row<h;row++)
        std::memcpy(target+(size_t)(y+row)*this->width+x,
                    src+(size_t)row*w,(size_t)w*sizeof(uint32_t));
}
// Decoded video bypasses UI scaling. 1080p inputs are copied 1:1.
void Scene2D::BlitVideoRgb32(const uint32_t* src,int x,int y,int w,int h) {
    if (!src||x<0||y<0||w<=0||h<=0||
        x>this->width||y>this->height||
        w>this->width-x||h>this->height-y) return;
    uint32_t* buffer=reinterpret_cast<uint32_t*>(
        this->frameBuffers[this->activeFrameBufferIdx]);
    for (int row=0;row<h;row++)
        std::memcpy(buffer+(size_t)(y+row)*this->width+x,
                    src+(size_t)row*w,(size_t)w*sizeof(uint32_t));
}
"""
Path(sys.argv[3]).write_text(out)
Path(sys.argv[4]).write_text(header)
print("Patched OpenOrbis graphics: 1080p video scanout and 720p logical UI")
