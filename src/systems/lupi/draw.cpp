#include "lupi_internal.hpp"
#include <algorithm>
#include <cstring>

using namespace nb;

// ---------------------------------------------------------------------------
// pixel-level helpers
// ---------------------------------------------------------------------------

static inline void apply_camera(const lupi_gfx_state& gfx, int& x, int& y)
{
    x -= gfx.camera_x;
    y -= gfx.camera_y;
}

// Matches lupinho's should_draw_pixel_with_pattern() (src/ui.c) exactly: an
// all-zero pattern (the default/reset state) always draws every pixel; a set
// bit means DRAW (an earlier version of this file had the polarity
// backwards — a set bit skipping the pixel — don't reintroduce that).
static inline bool fillp_should_draw(const lupi_gfx_state& gfx, int x, int y)
{
    bool all_zero = true;
    for (uint8_t b : gfx.fillp) if (b != 0) { all_zero = false; break; }
    if (all_zero) return true;

    uint8_t row = gfx.fillp[y & 7];
    return (row >> (7 - (x & 7))) & 1;
}

void nb::lupi_put_pixel(lupi_p& p, int x, int y, uint8_t color, bool is_fill_op)
{
    apply_camera(p.gfx, x, y);

    if (x < p.gfx.clip_x0 || x >= p.gfx.clip_x1 || y < p.gfx.clip_y0 || y >= p.gfx.clip_y1)
        return;
    if (x < 0 || x >= LUPI_SCREEN_W || y < 0 || y >= LUPI_SCREEN_H)
        return;

    if (is_fill_op && !fillp_should_draw(p.gfx, x, y))
        return;

    p.fb.at(x, y) = color;
}

// ---------------------------------------------------------------------------
// primitives
// ---------------------------------------------------------------------------

void nb::lupi_draw_cls(lupi_p& p, uint8_t color)
{
    std::memset(p.fb.pixels.data(), color, p.fb.pixels.size());
    p.gfx.reset_clip();
}

// (x1,y1) is an exclusive far corner, i.e. width = x1-x0, height = y1-y0 —
// confirmed against the real API (ui.rect/ui.rectfill build a width/height
// rect internally from these two corners, and draw a half-open [x0,x1)
// range, not an inclusive one — an earlier version of this file drew one
// extra pixel on both axes by treating x1/y1 as inclusive). No coordinate
// swapping/normalization either, matching the real API: a "backwards" rect
// (x1<x0 or y1<y0) draws nothing rather than auto-correcting.
void nb::lupi_draw_rect(lupi_p& p, int x0, int y0, int x1, int y1, uint8_t color)
{
    int w = x1 - x0, h = y1 - y0;
    for (int x = x0; x < x0 + w; ++x) {
        lupi_put_pixel(p, x, y0, color, true);
        lupi_put_pixel(p, x, y0 + h - 1, color, true);
    }
    for (int y = y0; y < y0 + h; ++y) {
        lupi_put_pixel(p, x0, y, color, true);
        lupi_put_pixel(p, x0 + w - 1, y, color, true);
    }
}

void nb::lupi_draw_rectfill(lupi_p& p, int x0, int y0, int x1, int y1, uint8_t color)
{
    int w = x1 - x0, h = y1 - y0;
    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x)
            lupi_put_pixel(p, x, y, color, true);
}

void nb::lupi_draw_circ(lupi_p& p, int cx, int cy, int r, uint8_t color)
{
    if (r < 0) return;
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        lupi_put_pixel(p, cx + x, cy + y, color, true);
        lupi_put_pixel(p, cx + y, cy + x, color, true);
        lupi_put_pixel(p, cx - y, cy + x, color, true);
        lupi_put_pixel(p, cx - x, cy + y, color, true);
        lupi_put_pixel(p, cx - x, cy - y, color, true);
        lupi_put_pixel(p, cx - y, cy - x, color, true);
        lupi_put_pixel(p, cx + y, cy - x, color, true);
        lupi_put_pixel(p, cx + x, cy - y, color, true);
        ++y;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            --x;
            err += 2 * (y - x) + 1;
        }
    }
}

void nb::lupi_draw_circfill(lupi_p& p, int cx, int cy, int r, uint8_t color)
{
    if (r < 0) return;
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        for (int sx = cx - x; sx <= cx + x; ++sx) {
            lupi_put_pixel(p, sx, cy + y, color, true);
            lupi_put_pixel(p, sx, cy - y, color, true);
        }
        for (int sx = cx - y; sx <= cx + y; ++sx) {
            lupi_put_pixel(p, sx, cy + x, color, true);
            lupi_put_pixel(p, sx, cy - x, color, true);
        }
        ++y;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            --x;
            err += 2 * (y - x) + 1;
        }
    }
}

void nb::lupi_draw_line(lupi_p& p, int x0, int y0, int x1, int y1, uint8_t color)
{
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        lupi_put_pixel(p, x0, y0, color, true);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

namespace {

struct vtx { int x, y; };

// Fills one triangle edge-pair's scanline span. Used by lupi_draw_trisfill after
// sorting vertices by y and splitting into a flat-bottom + flat-top half.
void fill_flat(lupi_p& p, vtx a, vtx b, vtx c, uint8_t color)
{
    // a is the lone vertex, b/c share a y (the flat edge)
    if (b.x > c.x) std::swap(b, c);
    int y0 = a.y, y1 = b.y;
    int dir = (y1 >= y0) ? 1 : -1;
    if (y0 == y1) return;
    for (int y = y0; y != y1 + dir; y += dir) {
        float t = float(y - a.y) / float(b.y - a.y);
        float xl = a.x + t * (b.x - a.x);
        float xr = a.x + t * (c.x - a.x);
        if (xl > xr) std::swap(xl, xr);
        for (int x = int(xl); x <= int(xr); ++x)
            lupi_put_pixel(p, x, y, color, true);
    }
}

}

void nb::lupi_draw_trisfill(lupi_p& p, int x1, int y1, int x2, int y2, int x3, int y3, uint8_t color)
{
    vtx v[3] = {{x1,y1},{x2,y2},{x3,y3}};
    std::sort(v, v+3, [](const vtx& a, const vtx& b){ return a.y < b.y; });

    if (v[0].y == v[2].y) { // degenerate: fully horizontal
        int xl = std::min({v[0].x, v[1].x, v[2].x});
        int xr = std::max({v[0].x, v[1].x, v[2].x});
        for (int x = xl; x <= xr; ++x)
            lupi_put_pixel(p, x, v[0].y, color, true);
        return;
    }

    if (v[1].y == v[2].y) {
        fill_flat(p, v[0], v[1], v[2], color);
        return;
    }
    if (v[0].y == v[1].y) {
        fill_flat(p, v[2], v[0], v[1], color);
        return;
    }

    // split at v[1].y along the long edge v[0]->v[2]
    float t = float(v[1].y - v[0].y) / float(v[2].y - v[0].y);
    vtx split { int(v[0].x + t * (v[2].x - v[0].x)), v[1].y };
    fill_flat(p, v[0], v[1], split, color);
    fill_flat(p, v[2], v[1], split, color);
}

void nb::lupi_draw_tile(lupi_p& p, const lupi_spritesheet& sheet, int tile_id, int x, int y,
                         bool flip_x, bool flip_y)
{
    const uint8_t* origin = sheet.tile_origin(tile_id);
    if (!origin) return;

    // check if tile is inside viewport, consider camera
    const int screen_x = x - p.gfx.camera_x;
    const int screen_y = y - p.gfx.camera_y;
    if (screen_x + sheet.tile_width <= 0 || screen_x >= LUPI_SCREEN_W ||
        screen_y + sheet.tile_height <= 0 || screen_y >= LUPI_SCREEN_H)
        return;

    int stride = sheet.image_width;
    int tw = sheet.tile_width, th = sheet.tile_height;
    for (int row = 0; row < th; ++row) {
        for (int col = 0; col < tw; ++col) {
            int sx = flip_x ? (tw - 1 - col) : col;
            int sy = flip_y ? (th - 1 - row) : row;
            uint8_t idx = origin[sy * stride + sx];
            if (idx == 0) continue; // palette index 0 in sheet-space = transparent
            lupi_put_pixel(p, x + col, y + row, idx, false);
        }
    }
}

// ---------------------------------------------------------------------------
// text: the REAL Lupi console's own fixed 5x8 bitmap font — transcribed from
// lupinho's (the official Lupi simulator, github.com/lupi-org-br/lupinho) own
// src/font.h, NOT self-authored. Confirmed against lupinho's src/ui.c
// draw_print(): column-major, bit 0 = topmost pixel; covers ASCII 32 (' ')
// through 126 ('~') only — any character outside that range is silently
// skipped (no glyph drawn, no cursor advance, matches the real engine
// exactly), including '\n' — the real console's ui.print() does NOT support
// multi-line text at all, so unlike an earlier version of this file, '\n' is
// not special-cased into a line break here either.
// ---------------------------------------------------------------------------

namespace {

constexpr int FONT_CHAR_WIDTH   = 5;
constexpr int FONT_CHAR_HEIGHT  = 8;
constexpr int FONT_CHAR_ADVANCE = 6; // FONT_CHAR_WIDTH + 1px spacing

// clang-format off
constexpr uint8_t font_data[95][5] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00 }, /* ' ' 32 */
    { 0x00, 0x00, 0x5F, 0x00, 0x00 }, /* '!' 33 */
    { 0x00, 0x07, 0x00, 0x07, 0x00 }, /* '"' 34 */
    { 0x14, 0x7F, 0x14, 0x7F, 0x14 }, /* '#' 35 */
    { 0x24, 0x2A, 0x7F, 0x2A, 0x12 }, /* '$' 36 */
    { 0x23, 0x13, 0x08, 0x64, 0x62 }, /* '%' 37 */
    { 0x36, 0x49, 0x55, 0x22, 0x50 }, /* '&' 38 */
    { 0x00, 0x05, 0x03, 0x00, 0x00 }, /* '\'' 39 */
    { 0x00, 0x1C, 0x22, 0x41, 0x00 }, /* '(' 40 */
    { 0x00, 0x41, 0x22, 0x1C, 0x00 }, /* ')' 41 */
    { 0x0A, 0x04, 0x1F, 0x04, 0x0A }, /* '*' 42 */
    { 0x08, 0x08, 0x3E, 0x08, 0x08 }, /* '+' 43 */
    { 0x00, 0x50, 0x30, 0x00, 0x00 }, /* ',' 44 */
    { 0x08, 0x08, 0x08, 0x08, 0x08 }, /* '-' 45 */
    { 0x00, 0x60, 0x60, 0x00, 0x00 }, /* '.' 46 */
    { 0x20, 0x10, 0x08, 0x04, 0x02 }, /* '/' 47 */
    { 0x3E, 0x51, 0x49, 0x45, 0x3E }, /* '0' 48 */
    { 0x00, 0x42, 0x7F, 0x40, 0x00 }, /* '1' 49 */
    { 0x42, 0x61, 0x51, 0x49, 0x46 }, /* '2' 50 */
    { 0x21, 0x41, 0x45, 0x4B, 0x31 }, /* '3' 51 */
    { 0x18, 0x14, 0x12, 0x7F, 0x10 }, /* '4' 52 */
    { 0x27, 0x45, 0x45, 0x45, 0x39 }, /* '5' 53 */
    { 0x3C, 0x4A, 0x49, 0x49, 0x30 }, /* '6' 54 */
    { 0x01, 0x71, 0x09, 0x05, 0x03 }, /* '7' 55 */
    { 0x36, 0x49, 0x49, 0x49, 0x36 }, /* '8' 56 */
    { 0x06, 0x49, 0x49, 0x29, 0x1E }, /* '9' 57 */
    { 0x00, 0x36, 0x36, 0x00, 0x00 }, /* ':' 58 */
    { 0x00, 0x56, 0x36, 0x00, 0x00 }, /* ';' 59 */
    { 0x00, 0x08, 0x14, 0x22, 0x41 }, /* '<' 60 */
    { 0x14, 0x14, 0x14, 0x14, 0x14 }, /* '=' 61 */
    { 0x41, 0x22, 0x14, 0x08, 0x00 }, /* '>' 62 */
    { 0x02, 0x01, 0x51, 0x09, 0x06 }, /* '?' 63 */
    { 0x32, 0x49, 0x79, 0x41, 0x3E }, /* '@' 64 */
    { 0x7E, 0x11, 0x11, 0x11, 0x7E }, /* 'A' 65 */
    { 0x7F, 0x49, 0x49, 0x49, 0x36 }, /* 'B' 66 */
    { 0x3E, 0x41, 0x41, 0x41, 0x22 }, /* 'C' 67 */
    { 0x7F, 0x41, 0x41, 0x22, 0x1C }, /* 'D' 68 */
    { 0x7F, 0x49, 0x49, 0x49, 0x41 }, /* 'E' 69 */
    { 0x7F, 0x09, 0x09, 0x09, 0x01 }, /* 'F' 70 */
    { 0x3E, 0x41, 0x49, 0x49, 0x7A }, /* 'G' 71 */
    { 0x7F, 0x08, 0x08, 0x08, 0x7F }, /* 'H' 72 */
    { 0x00, 0x41, 0x7F, 0x41, 0x00 }, /* 'I' 73 */
    { 0x20, 0x40, 0x41, 0x3F, 0x01 }, /* 'J' 74 */
    { 0x7F, 0x08, 0x14, 0x22, 0x41 }, /* 'K' 75 */
    { 0x7F, 0x40, 0x40, 0x40, 0x40 }, /* 'L' 76 */
    { 0x7F, 0x02, 0x04, 0x02, 0x7F }, /* 'M' 77 */
    { 0x7F, 0x04, 0x08, 0x10, 0x7F }, /* 'N' 78 */
    { 0x3E, 0x41, 0x41, 0x41, 0x3E }, /* 'O' 79 */
    { 0x7F, 0x09, 0x09, 0x09, 0x06 }, /* 'P' 80 */
    { 0x3E, 0x41, 0x51, 0x21, 0x5E }, /* 'Q' 81 */
    { 0x7F, 0x09, 0x19, 0x29, 0x46 }, /* 'R' 82 */
    { 0x46, 0x49, 0x49, 0x49, 0x31 }, /* 'S' 83 */
    { 0x01, 0x01, 0x7F, 0x01, 0x01 }, /* 'T' 84 */
    { 0x3F, 0x40, 0x40, 0x40, 0x3F }, /* 'U' 85 */
    { 0x1F, 0x20, 0x40, 0x20, 0x1F }, /* 'V' 86 */
    { 0x3F, 0x40, 0x38, 0x40, 0x3F }, /* 'W' 87 */
    { 0x63, 0x14, 0x08, 0x14, 0x63 }, /* 'X' 88 */
    { 0x07, 0x08, 0x70, 0x08, 0x07 }, /* 'Y' 89 */
    { 0x61, 0x51, 0x49, 0x45, 0x43 }, /* 'Z' 90 */
    { 0x00, 0x7F, 0x41, 0x41, 0x00 }, /* '[' 91 */
    { 0x02, 0x04, 0x08, 0x10, 0x20 }, /* '\\' 92 */
    { 0x00, 0x41, 0x41, 0x7F, 0x00 }, /* ']' 93 */
    { 0x04, 0x02, 0x01, 0x02, 0x04 }, /* '^' 94 */
    { 0x40, 0x40, 0x40, 0x40, 0x40 }, /* '_' 95 */
    { 0x00, 0x01, 0x02, 0x04, 0x00 }, /* '`' 96 */
    { 0x20, 0x54, 0x54, 0x54, 0x78 }, /* 'a' 97 */
    { 0x7F, 0x48, 0x44, 0x44, 0x38 }, /* 'b' 98 */
    { 0x38, 0x44, 0x44, 0x44, 0x20 }, /* 'c' 99 */
    { 0x38, 0x44, 0x44, 0x48, 0x7F }, /* 'd' 100 */
    { 0x38, 0x54, 0x54, 0x54, 0x18 }, /* 'e' 101 */
    { 0x08, 0x7E, 0x09, 0x01, 0x02 }, /* 'f' 102 */
    { 0x0C, 0x52, 0x52, 0x52, 0x3E }, /* 'g' 103 */
    { 0x7F, 0x08, 0x04, 0x04, 0x78 }, /* 'h' 104 */
    { 0x00, 0x44, 0x7D, 0x40, 0x00 }, /* 'i' 105 */
    { 0x20, 0x40, 0x44, 0x3D, 0x00 }, /* 'j' 106 */
    { 0x7F, 0x10, 0x28, 0x44, 0x00 }, /* 'k' 107 */
    { 0x00, 0x41, 0x7F, 0x40, 0x00 }, /* 'l' 108 */
    { 0x7C, 0x04, 0x18, 0x04, 0x78 }, /* 'm' 109 */
    { 0x7C, 0x08, 0x04, 0x04, 0x78 }, /* 'n' 110 */
    { 0x38, 0x44, 0x44, 0x44, 0x38 }, /* 'o' 111 */
    { 0x7C, 0x14, 0x14, 0x14, 0x08 }, /* 'p' 112 */
    { 0x08, 0x14, 0x14, 0x14, 0x7C }, /* 'q' 113 */
    { 0x7C, 0x08, 0x04, 0x04, 0x08 }, /* 'r' 114 */
    { 0x48, 0x54, 0x54, 0x54, 0x20 }, /* 's' 115 */
    { 0x04, 0x3F, 0x44, 0x40, 0x20 }, /* 't' 116 */
    { 0x3C, 0x40, 0x40, 0x20, 0x7C }, /* 'u' 117 */
    { 0x1C, 0x20, 0x40, 0x20, 0x1C }, /* 'v' 118 */
    { 0x3C, 0x40, 0x30, 0x40, 0x3C }, /* 'w' 119 */
    { 0x44, 0x28, 0x10, 0x28, 0x44 }, /* 'x' 120 */
    { 0x0C, 0x50, 0x50, 0x50, 0x3C }, /* 'y' 121 */
    { 0x44, 0x64, 0x54, 0x4C, 0x44 }, /* 'z' 122 */
    { 0x00, 0x08, 0x36, 0x41, 0x00 }, /* '{' 123 */
    { 0x00, 0x00, 0x7F, 0x00, 0x00 }, /* '|' 124 */
    { 0x00, 0x41, 0x36, 0x08, 0x00 }, /* '}' 125 */
    { 0x10, 0x08, 0x08, 0x10, 0x08 }, /* '~' 126 */
};
// clang-format on

}

void nb::lupi_draw_print(lupi_p& p, const char* text, int x, int y, uint8_t color)
{
    int cursor_x = x;
    for (const char* c = text; *c; ++c) {
        uint8_t ch = (uint8_t)*c;
        if (ch < 32 || ch > 126) continue;

        const uint8_t* glyph = font_data[ch - 32];
        for (int col = 0; col < FONT_CHAR_WIDTH; ++col)
            for (int row = 0; row < FONT_CHAR_HEIGHT; ++row)
                if (glyph[col] & (1 << row))
                    lupi_put_pixel(p, cursor_x + col, y + row, color, false);

        cursor_x += FONT_CHAR_ADVANCE;
    }
}
