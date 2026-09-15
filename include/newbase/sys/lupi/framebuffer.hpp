#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace nb {

static constexpr int LUPI_SCREEN_W      = 480;
static constexpr int LUPI_SCREEN_H      = 270;
static constexpr int LUPI_PALETTE_SIZE  = 256;

// 480x270 indexed-color software framebuffer — one palette index per pixel,
// matching the real Lupi console's 256-color indexed display.
struct lupi_framebuffer {
    std::vector<uint8_t> pixels;

    lupi_framebuffer() : pixels(static_cast<size_t>(LUPI_SCREEN_W) * LUPI_SCREEN_H, 0) {}

    uint8_t& at(int x, int y)       { return pixels[static_cast<size_t>(y) * LUPI_SCREEN_W + x]; }
    uint8_t  at(int x, int y) const { return pixels[static_cast<size_t>(y) * LUPI_SCREEN_W + x]; }
};

// Palette entries are stored exactly as ui.palset receives them and as the
// real Lupi image codec emits them: BGR555 (blue in bits 14-10, green in bits
// 9-5, red in bits 4-0) — e.g. white = 0x7FFF, pure blue = 0x7C00. Confirmed
// against lupi-codec's colors_convert.lua (rgb_to_bgr555: bor(b5<<10, g5<<5, r5)).
// Converted to RGBA8888 only once per frame, at the RENDER-phase blit into
// the GPU texture.
struct lupi_palette {
    std::array<uint16_t, LUPI_PALETTE_SIZE> bgr555 {};
    // tracks which entries have been explicitly set (via ui.palset or spritesheet
    // auto-palettize), so the spritesheet loader's allocator (loaders.cpp) can
    // tell an unused slot apart from one legitimately holding BGR555 0x0000.
    std::array<bool, LUPI_PALETTE_SIZE> allocated {};

    void set(int index, uint16_t color)
    {
        if (index >= 0 && index < LUPI_PALETTE_SIZE) {
            bgr555[index] = color;
            allocated[index] = true;
        }
    }

    static uint32_t bgr555_to_rgba8888(uint16_t c)
    {
        uint8_t b5 = (c >> 10) & 0x1F;
        uint8_t g5 = (c >> 5)  & 0x1F;
        uint8_t r5 =  c        & 0x1F;
        uint8_t r8 = (r5 << 3) | (r5 >> 2);
        uint8_t g8 = (g5 << 3) | (g5 >> 2);
        uint8_t b8 = (b5 << 3) | (b5 >> 2);
        return (uint32_t)r8 | ((uint32_t)g8 << 8) | ((uint32_t)b8 << 16) | (0xFFu << 24);
    }
};

// Per-frame draw state, reset (partially) by ui.cls(). Every drawing primitive
// applies camera offset, clip rect, and (for fill ops only) the fillp dither
// pattern through the single put_pixel() helper in draw.cpp, so this state is
// interpreted identically everywhere.
struct lupi_gfx_state {
    int clip_x0 = 0, clip_y0 = 0, clip_x1 = LUPI_SCREEN_W, clip_y1 = LUPI_SCREEN_H;
    int camera_x = 0, camera_y = 0;
    uint8_t fillp[8] = {0,0,0,0,0,0,0,0}; // 8x8 dither bitmask, row-major, bit0 = leftmost pixel

    void reset_clip() { clip_x0 = 0; clip_y0 = 0; clip_x1 = LUPI_SCREEN_W; clip_y1 = LUPI_SCREEN_H; }
};

}
