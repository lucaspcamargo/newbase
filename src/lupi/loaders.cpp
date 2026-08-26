#include "lupi_internal.hpp"
#include <newbase/res/manager.hpp>
#include <newbase/res/script.hpp>
#include <newbase/log.hpp>
#include <ryml_std.hpp>
#include <stb_image.h>
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <unordered_set>

using namespace nb;

// ---------------------------------------------------------------------------
// 0b binary literal preprocessing (see lupi_internal.hpp for why)
// ---------------------------------------------------------------------------

std::string nb::lupi_preprocess_binary_literals(const std::string& src)
{
    std::string out;
    out.reserve(src.size());
    size_t i = 0, n = src.size();
    auto is_ident = [](char c) { return std::isalnum((unsigned char)c) || c == '_'; };

    while (i < n) {
        char c = src[i];

        // -- comments: line "--..." or long "--[[...]]" / "--[=[...]=]" --
        if (c == '-' && i + 1 < n && src[i + 1] == '-') {
            size_t start = i;
            i += 2;
            if (i < n && src[i] == '[') {
                size_t j = i + 1;
                int level = 0;
                while (j < n && src[j] == '=') { ++level; ++j; }
                if (j < n && src[j] == '[') {
                    std::string close = "]" + std::string(level, '=') + "]";
                    size_t end = src.find(close, j + 1);
                    size_t stop = (end == std::string::npos) ? n : end + close.size();
                    out.append(src, start, stop - start);
                    i = stop;
                    continue;
                }
            }
            size_t eol = src.find('\n', i);
            size_t stop = (eol == std::string::npos) ? n : eol;
            out.append(src, start, stop - start);
            i = stop;
            continue;
        }

        // -- short string literals "..." / '...' --
        if (c == '"' || c == '\'') {
            size_t start = i;
            char quote = c;
            ++i;
            while (i < n && src[i] != quote) {
                if (src[i] == '\\' && i + 1 < n) i += 2;
                else ++i;
            }
            if (i < n) ++i;
            out.append(src, start, i - start);
            continue;
        }

        // -- long string literals [[...]] / [=[...]=] --
        if (c == '[') {
            size_t j = i + 1;
            int level = 0;
            while (j < n && src[j] == '=') { ++level; ++j; }
            if (j < n && src[j] == '[') {
                size_t start = i;
                std::string close = "]" + std::string(level, '=') + "]";
                size_t end = src.find(close, j + 1);
                size_t stop = (end == std::string::npos) ? n : end + close.size();
                out.append(src, start, stop - start);
                i = stop;
                continue;
            }
        }

        // -- 0b/0B binary literal --
        if (c == '0' && i + 1 < n && (src[i + 1] == 'b' || src[i + 1] == 'B')
            && (i == 0 || !is_ident(src[i - 1]))) {
            size_t digits_start = i + 2;
            size_t j = digits_start;
            while (j < n && (src[j] == '0' || src[j] == '1')) ++j;
            if (j > digits_start && (j >= n || !is_ident(src[j]))) {
                unsigned long long value = 0;
                for (size_t k = digits_start; k < j; ++k)
                    value = (value << 1) | (unsigned long long)(src[k] - '0');
                out += std::to_string(value);
                i = j;
                continue;
            }
        }

        out += c;
        ++i;
    }
    return out;
}

namespace {

// Directory portion (trailing slash included) of a resource's own path, e.g.
// "res/lupi_demo/lupi-balao-gatinho/lupi.yaml" -> "res/lupi_demo/lupi-balao-gatinho/".
std::string dir_of_resource(entt::id_type id)
{
    const auto& handles = rman().handles();
    auto it = handles.find(id);
    if (it == handles.end() || it->second.path.empty())
        return {};
    const std::string& base = it->second.path;
    auto slash = base.rfind('/');
    if (slash == std::string::npos)
        return {};
    return base.substr(0, slash + 1);
}

// Filename stem: last path segment, minus its extension.
std::string stem_of(const std::string& path)
{
    auto slash = path.rfind('/');
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    auto dot = name.rfind('.');
    return (dot == std::string::npos) ? name : name.substr(0, dot);
}

bool has_suffix(const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}

// ---------------------------------------------------------------------------
// .lupicart loader — real Lupi carts: a `lupi.yaml` manifest (cosmetic
// metadata only) sitting next to `game.lua`. No asset list — see cart.hpp.
// ---------------------------------------------------------------------------

rloader_lupi_cart::result_type rloader_lupi_cart::operator()(entt::id_type id) const
{
    log::info("[rloader_lupi_cart] loading: %x", id);

    std::vector<char> data;
    if (!rman().read_all_sync(id, data, true)) {
        log::error("[rloader_lupi_cart] cannot read: %x", id);
        return nullptr;
    }

    // Parsed only for logging — real carts declare no fields we depend on.
    auto tree = ryml::parse_in_place(c4::to_substr(data.data()));
    auto root = tree.rootref();
    std::string name, version;
    if (root.has_child("name"))    root["name"]    >> name;
    if (root.has_child("version")) { std::string v; root["version"] >> v; version = v; }

    auto ret = std::make_shared<rlupi_cart>(id);
    ret->dir = dir_of_resource(id);

    auto game_lua_path = ret->dir + "game.lua";
    auto main_id = entt::hashed_string{game_lua_path.c_str()}.value();
    auto script = rman().get<rscript>(main_id);
    if (!script || !script->valid) {
        log::error("[rloader_lupi_cart] cannot load '%s': %x", game_lua_path.c_str(), id);
        return nullptr;
    }
    ret->main_lua_src = lupi_preprocess_binary_literals(
        std::string(script->raw.begin(), script->raw.end()));
    ret->chunkname = script->chunkname;

    ret->valid = true;
    log::info("[rloader_lupi_cart] '%s' v%s, dir='%s'", name.c_str(), version.c_str(), ret->dir.c_str());
    return ret;
}

// ---------------------------------------------------------------------------
// spritesheet auto-palettize loader (not an nb::resource — see lupi_internal.hpp)
// ---------------------------------------------------------------------------

namespace {

// The real lupi-codec's tile-slicing marker: a specific mid-gray BGR555 value
// (8456 = bor(8<<10, 8<<5, 8), i.e. r5=g5=b5=8) painted at the source image's
// (0,0) pixel and along the entire margin row/column of a multi-tile sheet.
// See lupi-codec's tile_detector.lua. If (0,0) doesn't match, the whole image
// is one single-tile "sheet" (used for standalone sprites like hero frames).
constexpr uint16_t MAGIC_COLOR_BGR555 = 8456;

uint16_t rgb8_to_bgr555(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t r5 = r >> 3, g5 = g >> 3, b5 = b >> 3;
    return (uint16_t)((b5 << 10) | (g5 << 5) | r5);
}

// Palette index 0 is reserved as the sprite-transparency marker (lupi_draw_tile
// treats it that way), so this only ever allocates/matches into indices 1-255.
// Exact BGR555 match first (matching lupi-codec's PixelIndexer exactly — no
// dithering/rounding, just a 5-bit truncating shift); if the palette isn't
// full yet, allocate a new slot; otherwise fall back to the nearest existing
// entry by channel distance (a deliberate, pragmatic deviation from the real
// codec, which has no fallback and simply can't exceed 256 colors in a
// released cart — we're more lenient so an over-budget cart still renders).
int find_or_allocate_color(lupi_palette& pal, uint16_t color)
{
    for (int i = 1; i < LUPI_PALETTE_SIZE; ++i)
        if (pal.allocated[i] && pal.bgr555[i] == color)
            return i;

    for (int i = 1; i < LUPI_PALETTE_SIZE; ++i) {
        if (!pal.allocated[i]) {
            pal.bgr555[i] = color;
            pal.allocated[i] = true;
            return i;
        }
    }

    auto chan = [](uint16_t c, int shift) { return (c >> shift) & 0x1F; };
    int b0 = chan(color, 10), g0 = chan(color, 5), r0 = chan(color, 0);
    int best = 1, best_dist = INT_MAX;
    for (int i = 1; i < LUPI_PALETTE_SIZE; ++i) {
        int b1 = chan(pal.bgr555[i], 10), g1 = chan(pal.bgr555[i], 5), r1 = chan(pal.bgr555[i], 0);
        int d = (r0 - r1) * (r0 - r1) + (g0 - g1) * (g0 - g1) + (b0 - b1) * (b0 - b1);
        if (d < best_dist) { best_dist = d; best = i; }
    }
    return best;
}

// Palette.hex's own allocator (see the closure below for why this can't just
// call find_or_allocate_color on the live `pal`): searches p.master_pal — the
// frozen, boot-time snapshot of the cart's synthesized palette — instead of
// `pal`, which a cart is free to clobber wholesale at runtime (ui.palset fade
// effects). A color only ever gets a *new* slot the first time it's ever
// requested and wasn't already part of the cart's own sprite-derived palette;
// once assigned, that mapping is permanent for the cart's lifetime. New slots
// are mirrored into `pal` too, in lockstep, so rendering can actually use them.
int find_in_master_or_allocate(lupi_p& p, uint16_t color)
{
    for (int i = 1; i < LUPI_PALETTE_SIZE; ++i)
        if (p.master_pal.allocated[i] && p.master_pal.bgr555[i] == color)
            return i;

    for (int i = 1; i < LUPI_PALETTE_SIZE; ++i) {
        if (!p.master_pal.allocated[i]) {
            p.master_pal.set(i, color);
            p.pal.set(i, color);
            return i;
        }
    }

    auto chan = [](uint16_t c, int shift) { return (c >> shift) & 0x1F; };
    int b0 = chan(color, 10), g0 = chan(color, 5), r0 = chan(color, 0);
    int best = 1, best_dist = INT_MAX;
    for (int i = 1; i < LUPI_PALETTE_SIZE; ++i) {
        int b1 = chan(p.master_pal.bgr555[i], 10), g1 = chan(p.master_pal.bgr555[i], 5), r1 = chan(p.master_pal.bgr555[i], 0);
        int d = (r0 - r1) * (r0 - r1) + (g0 - g1) * (g0 - g1) + (b0 - b1) * (b0 - b1);
        if (d < best_dist) { best_dist = d; best = i; }
    }
    return best;
}

}

std::shared_ptr<lupi_spritesheet> nb::lupi_load_spritesheet_indexed(
    const std::vector<char>& png_bytes, lupi_palette& pal, const std::string& path)
{
    int w, h, chs;
    auto* rgba = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc*>(png_bytes.data()), (int)png_bytes.size(), &w, &h, &chs, 4);
    if (!rgba) {
        log::error("[lupi_load_spritesheet_indexed] stbi decode failed for '%s'", path.c_str());
        return nullptr;
    }

    // Mirrors lupi-codec's image_validator.lua: the real compiler never ships
    // (deletes at build time) an image over 512x512, or with more than 256
    // unique colors — a real cart just can't reference a file the pipeline
    // refused to build in the first place. We can't refuse to ship (the cart
    // is already running), but silently accepting either just means a stray
    // asset — e.g. a project's own reference/preview PNG that isn't meant to
    // be a game asset at all — can quietly exhaust the cart's whole 256-slot
    // palette. Reject the same way instead: the file just never becomes a
    // usable sprite (Sprites.find() sees nothing for it), same as on real
    // hardware.
    if (w > 512 || h > 512) {
        log::error("[lupi_load_spritesheet_indexed] '%s' is %dx%d, exceeds lupi-codec's 512x512 limit, skipping",
                    path.c_str(), w, h);
        stbi_image_free(rgba);
        return nullptr;
    }

    auto px = [&](int x, int y) -> const uint8_t* { return rgba + (static_cast<size_t>(y) * w + x) * 4; };

    {
        std::unordered_set<uint32_t> unique_colors;
        for (int y = 0; y < h && unique_colors.size() <= 256; ++y) {
            for (int x = 0; x < w; ++x) {
                const uint8_t* c = px(x, y);
                unique_colors.insert((uint32_t)c[0] << 24 | (uint32_t)c[1] << 16 | (uint32_t)c[2] << 8 | c[3]);
                if (unique_colors.size() > 256) break;
            }
        }
        if (unique_colors.size() > 256) {
            log::error("[lupi_load_spritesheet_indexed] '%s' has more than 256 unique colors, skipping",
                        path.c_str());
            stbi_image_free(rgba);
            return nullptr;
        }
    }

    // --- magic-marker tile detection (mirrors lupi-codec's tile_detector.lua) ---
    bool is_tiled = false;
    int guide_w = w, guide_h = h;
    {
        const uint8_t* c0 = px(0, 0);
        if (rgb8_to_bgr555(c0[0], c0[1], c0[2]) == MAGIC_COLOR_BGR555) {
            int gw = 0;
            for (int x = 0; x < w; ++x) {
                const uint8_t* c = px(x, 0);
                if (rgb8_to_bgr555(c[0], c[1], c[2]) != MAGIC_COLOR_BGR555) { gw = x; break; }
                gw = x + 1;
            }
            int gh = 0;
            for (int y = 0; y < h; ++y) {
                const uint8_t* c = px(0, y);
                if (rgb8_to_bgr555(c[0], c[1], c[2]) != MAGIC_COLOR_BGR555) { gh = y; break; }
                gh = y + 1;
            }
            if (gw > 0 && gh > 0) { is_tiled = true; guide_w = gw; guide_h = gh; }
        }
    }

    auto sheet = std::make_shared<lupi_spritesheet>();
    sheet->image_width = w;
    sheet->pixels.resize(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint8_t* c = px(x, y);
            uint8_t idx;
            if (c[3] == 0) { // fully transparent only — matches lupi-codec's PixelIndexer exactly
                idx = 0;
            } else {
                idx = (uint8_t)find_or_allocate_color(pal, rgb8_to_bgr555(c[0], c[1], c[2]));
            }
            sheet->pixels[static_cast<size_t>(y) * w + x] = idx;
        }
    }
    stbi_image_free(rgba);

    if (is_tiled) {
        // The magic-colored guide occupies the entire first tile row AND
        // column (lupi-codec's ImageEncoder skips tile_x==0 || tile_y==0
        // entirely) — content tiles start at grid position (1,1).
        int grid_cols = w / guide_w, grid_rows = h / guide_h;
        sheet->tile_width  = guide_w;
        sheet->tile_height = guide_h;
        sheet->col_offset  = 1;
        sheet->row_offset  = 1;
        sheet->cols = std::max(0, grid_cols - 1);
        sheet->rows = std::max(0, grid_rows - 1);
        sheet->tile_count = sheet->cols * sheet->rows;
        if (sheet->tile_count > 1024) {
            log::warn("[lupi_load_spritesheet_indexed] %d tiles exceeds ui.tile's 0-1023 range, clamping",
                       sheet->tile_count);
            sheet->tile_count = 1024;
        }
    } else {
        sheet->tile_width  = w;
        sheet->tile_height = h;
        sheet->cols = 1;
        sheet->rows = 1;
        sheet->col_offset = 0;
        sheet->row_offset = 0;
        sheet->tile_count = 1;
    }

    log::info("[lupi_load_spritesheet_indexed] loaded %dx%d, tiled=%d, tile=%dx%d, %d tiles",
              w, h, is_tiled, sheet->tile_width, sheet->tile_height, sheet->tile_count);
    return sheet;
}

// ---------------------------------------------------------------------------
// Sprites/Palette auto-discovery — real carts ship no asset manifest at all;
// every .png under the cart's directory becomes a named sprite (filename
// stem, no directory prefix — matches real cart code's Sprites.find(name)
// call sites exactly, confirmed against a real cart's img/+maps/ layout).
// ---------------------------------------------------------------------------

namespace {

std::vector<std::string> split_path(const std::string& rel)
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= rel.size()) {
        size_t slash = rel.find('/', start);
        if (slash == std::string::npos) { parts.push_back(rel.substr(start)); break; }
        parts.push_back(rel.substr(start, slash - start));
        start = slash + 1;
    }
    return parts;
}

// Sets sprites_table[segments[0]][segments[1]]...[segments.back()] = a fresh
// sprite_ref for `sheet`, creating intermediate sub-tables as needed. A
// single-segment path (no subfolder) just sets a direct field. Matches a
// real cart's own nested Sprites.<folder>.<...>.<name> access convention
// (confirmed e.g. Sprites.tilemap.clouds, Sprites.poi.cherry["3"],
// Sprites.player.win.f1) — distinct from, and built alongside, the flat
// Sprites.find(name) convention another real cart uses instead.
void set_nested_sprite(lua_State* L, int sprites_idx, const std::vector<std::string>& segments,
                        std::shared_ptr<lupi_spritesheet> sheet)
{
    lua_pushvalue(L, sprites_idx);
    for (size_t i = 0; i + 1 < segments.size(); ++i) {
        lua_getfield(L, -1, segments[i].c_str());
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_setfield(L, -3, segments[i].c_str());
        }
        lua_remove(L, -2);
    }
    lupi_push_sprite_ref(L, std::move(sheet));
    lua_setfield(L, -2, segments.back().c_str());
    lua_pop(L, 1);
}

}

void nb::lupi_scan_cart_assets(lua_State* L, lupi_p& p)
{
    // Reserve any overridden indices FIRST, before a single sprite color is
    // allocated — find_or_allocate_color's "first free slot" search then
    // skips them automatically, so a real sprite color can never land on
    // (and later get silently overwritten at) an overridden index. Applying
    // overrides after the scan (the original approach) meant they could
    // clobber whatever real color the scan happened to put there first.
    lupi_apply_palette_overrides(p);

    // rman().handles() is an unordered_map — iterating it directly makes
    // palette allocation order (and therefore which color lands at which
    // index) depend on unrelated resources elsewhere in the same table
    // (confirmed: adding a second cart's files shifted this cart's own
    // hash-bucket layout enough to visibly change its sprite colors, even
    // though the path filter below only ever processes this cart's own
    // files). Sort first so allocation order only depends on this cart's
    // own asset paths, stable regardless of what else is loaded.
    std::vector<std::pair<entt::id_type, std::string>> matches;
    for (const auto& [res_id, handle] : rman().handles()) {
        if (handle.path.compare(0, p.cart_dir.size(), p.cart_dir) != 0) continue;
        if (!has_suffix(handle.path, ".png")) continue;
        matches.emplace_back(res_id, handle.path);
    }
    std::sort(matches.begin(), matches.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    std::vector<std::pair<std::string, std::shared_ptr<lupi_spritesheet>>> discovered;
    for (const auto& [res_id, path] : matches) {
        std::vector<char> bytes;
        if (!rman().read_all_sync(res_id, bytes, false)) {
            log::error("[lupi_scan_cart_assets] cannot read '%s'", path.c_str());
            continue;
        }
        auto sheet = lupi_load_spritesheet_indexed(bytes, p.pal, path);
        if (!sheet) continue;

        std::string rel = path.substr(p.cart_dir.size());
        rel = rel.substr(0, rel.size() - 4); // strip ".png"
        p.sprite_by_name[stem_of(path)] = sheet;
        const int image_height = sheet->image_width > 0
            ? static_cast<int>(sheet->pixels.size() / static_cast<size_t>(sheet->image_width))
            : 0;
        const std::string extras = sheet->cols == 1 && sheet->rows == 1
            ? "(no frames)"
            : "tiles " + std::to_string(sheet->tile_width) + "x" + std::to_string(sheet->tile_height) +
                  ", " + std::to_string(sheet->cols) + "x" + std::to_string(sheet->rows) +
                  " (" + std::to_string(sheet->tile_count) + ")";
        p.assets.push_back({
            "Sprite",
            stem_of(path),
            std::to_string(sheet->image_width) + "x" + std::to_string(image_height),
            extras,
            rman().handles().at(res_id).size,
            res_id,
            path
        });
        discovered.emplace_back(std::move(rel), std::move(sheet));
    }
    log::info("[lupi_scan_cart_assets] discovered %zu sprite(s) under '%s'",
              discovered.size(), p.cart_dir.c_str());

    // --- Sprites = { find = function(name) ... end, <nested folder tree> } ---
    lua_newtable(L);
    int sprites_idx = lua_gettop(L);
    lua_pushlightuserdata(L, &p);
    lua_pushcclosure(L, +[](lua_State* L) -> int {
        auto* p = static_cast<lupi_p*>(lua_touserdata(L, lua_upvalueindex(1)));
        const char* name = luaL_checkstring(L, 1);
        auto it = p->sprite_by_name.find(name);
        if (it == p->sprite_by_name.end()) {
            log::warn("[lupi] Sprites.find('%s'): not found", name);
            lua_pushnil(L);
            return 1;
        }
        lupi_push_sprite_ref(L, it->second);
        return 1;
    }, 1);
    lua_setfield(L, sprites_idx, "find");

    for (auto& [rel, sheet] : discovered)
        set_nested_sprite(L, sprites_idx, split_path(rel), std::move(sheet));

    lua_setglobal(L, "Sprites");

    // Overrides can reserve an index past the auto-scan's contiguous run
    // (e.g. index 49 when only ~26 real sprite colors got allocated) —
    // close any such gap so the Palette array below stays a proper Lua
    // sequence;
    // `for i=1,#Palette do ... end` (every real cart's own palette-apply
    // loop) needs a well-defined `#Palette`, which a sparse table doesn't
    // reliably give. Backfilled gaps default to black (0x0000, unallocated's
    // existing default) — harmless, since nothing else claims those slots.
    int max_index = -1;
    for (int i = 0; i < LUPI_PALETTE_SIZE; ++i)
        if (p.pal.allocated[i]) max_index = i;
    for (int i = 0; i <= max_index; ++i)
        p.pal.allocated[i] = true;

    // Freeze this as the cart's "master" palette — see lupi_p::master_pal and
    // find_in_master_or_allocate — before any of the cart's own Lua runs and
    // gets a chance to clobber `pal` at runtime (ui.palset fade effects, etc).
    p.master_pal = p.pal;

    // --- Palette = { [1]=bgr555, [2]=bgr555, ..., hex = function(rgb) ... end }
    // — our own synthesized equivalent of lupi-codec's generated palette.lua,
    // built from exactly the colors our own quantization pass just allocated.
    // `Palette.hex` is NOT part of the real, documented Lupi API (confirmed:
    // absent from lupi.api.br/docs, from lupinho's lua_api.c, and from
    // lupi-codec's generated palette.lua) — multiple real carts call it
    // anyway, expecting a pre-existing global exactly like Sprites/Palette,
    // so on real hardware they'd crash outright. We keep it as a compatible
    // stand-in rather than let those carts fail to boot at all.
    //
    // It resolves against p.master_pal, NOT the live `pal`, via
    // find_in_master_or_allocate — see that function for why: some carts dim
    // their entire runtime palette every frame via a
    // `for i=1,#Palette do ui.palset(i-1, dim(Palette[i])) end` loop, and an
    // exact-match search against the just-dimmed `pal` would fail almost
    // every call (dimmed != original), allocating a *new* slot each time and
    // burning through the 256-slot budget in roughly one fade's worth of
    // frames — then, once exhausted, silently reusing already-dimmed
    // neighbors as if they were original colors, visibly darkening a bit
    // more with every subsequent fade, permanently.
    lua_newtable(L);
    int n = 0;
    for (int i = 0; i < LUPI_PALETTE_SIZE && p.pal.allocated[i]; ++i, ++n) {
        lua_pushinteger(L, p.pal.bgr555[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_pushcfunction(L, +[](lua_State* L) -> int {
        lupi_p* p = lupi_self(L);
        lua_Integer hex = luaL_checkinteger(L, 1);
        uint8_t r = (uint8_t)((hex >> 16) & 0xFF);
        uint8_t g = (uint8_t)((hex >> 8) & 0xFF);
        uint8_t b = (uint8_t)(hex & 0xFF);
        uint16_t bgr555 = rgb8_to_bgr555(r, g, b);

        int index = find_in_master_or_allocate(*p, bgr555);

        // Keep the Lua `Palette` table in sync with the master palette too —
        // a no-op unless this call just allocated a genuinely new slot.
        lua_getglobal(L, "Palette");
        lua_pushinteger(L, p->master_pal.bgr555[index]);
        lua_rawseti(L, -2, index + 1);
        lua_pop(L, 1);

        lua_pushinteger(L, index);
        return 1;
    });
    lua_setfield(L, -2, "hex");
    lua_setglobal(L, "Palette");
    log::info("[lupi_scan_cart_assets] synthesized Palette with %d entries", n);
}

// ---------------------------------------------------------------------------
// Palette overrides (per-cart YAML sidecar) — see doc/system_lupi.md's
// "Palette overrides" section for why this exists: a real cart's own
// hardcoded palette indices (e.g. Balão Gatinho's COLORS.LIGHT_BLUE = 18)
// only made sense against the original developer's master_palette.json,
// which is external to the cart's repo and permanently lost to us. This is
// newbase's own escape hatch, not part of the real Lupi format — entirely
// optional, absent for any cart we haven't manually annotated.
// ---------------------------------------------------------------------------

void nb::lupi_apply_palette_overrides(lupi_p& p)
{
    std::string path = p.cart_dir + "palette_overrides.yaml";
    auto id = entt::hashed_string{path.c_str()}.value();

    // Most carts have no sidecar at all — check first so the common case
    // doesn't trip rmanager's "unknown asset id" error log on every boot.
    if (!rman().known(id))
        return;

    std::vector<char> data;
    if (!rman().read_all_sync(id, data, true))
        return; // no sidecar for this cart — nothing to do

    auto tree = ryml::parse_in_place(c4::to_substr(data.data()));
    auto root = tree.rootref();
    if (!root.has_child("palette")) {
        log::warn("[lupi_apply_palette_overrides] '%s' has no 'palette' section, ignoring", path.c_str());
        return;
    }

    int n = 0;
    for (auto entry : root["palette"]) {
        int index = 0;
        c4::from_chars(entry.key(), &index);
        std::string hex;
        entry >> hex;
        if (!hex.empty() && hex[0] == '#') hex.erase(0, 1);
        if (hex.size() != 6 || index < 0 || index >= LUPI_PALETTE_SIZE) {
            log::warn("[lupi_apply_palette_overrides] '%s': skipping malformed entry (index=%d, hex='%s')",
                      path.c_str(), index, hex.c_str());
            continue;
        }
        unsigned long rgb = std::strtoul(hex.c_str(), nullptr, 16);
        uint8_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
        p.pal.set(index, rgb8_to_bgr555(r, g, b));
        ++n;
    }
    log::info("[lupi_apply_palette_overrides] applied %d override(s) from '%s'", n, path.c_str());
}
