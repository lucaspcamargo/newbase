#include "lupi_internal.hpp"
#include <newbase/log.hpp>
#include <algorithm>
#include <new>

using namespace nb;

namespace {
// Real cart code routinely passes float results (math.sin/division/etc) as
// pixel coordinates, radii, and even palette/color indices (confirmed at
// runtime: cloud.lua's ui.circfill(d + cloud.x - half_width, ...) where d is
// a math.sin() result) — luaL_checkinteger/optinteger reject non-exact-integer
// floats in Lua 5.4+, which real Lupi clearly doesn't. Truncate instead.
inline int checkint(lua_State* L, int idx) { return (int)luaL_checknumber(L, idx); }
inline int optint(lua_State* L, int idx, int def) { return (int)luaL_optnumber(L, idx, (lua_Number)def); }
}

// ---------------------------------------------------------------------------
// sprite_ref userdata
// ---------------------------------------------------------------------------

static int l_sprite_ref_gc(lua_State* L)
{
    auto* ud = static_cast<lupi_sprite_ref_userdata*>(luaL_checkudata(L, 1, "lupi.sprite_ref"));
    ud->~lupi_sprite_ref_userdata();
    return 0;
}

void nb::lupi_push_sprite_ref(lua_State* L, std::shared_ptr<lupi_spritesheet> sheet)
{
    auto* ud = static_cast<lupi_sprite_ref_userdata*>(lua_newuserdatauv(L, sizeof(lupi_sprite_ref_userdata), 0));
    new (ud) lupi_sprite_ref_userdata{std::move(sheet)};
    luaL_setmetatable(L, "lupi.sprite_ref");
}

lupi_spritesheet* nb::lupi_check_sprite_ref(lua_State* L, int idx)
{
    auto* ud = static_cast<lupi_sprite_ref_userdata*>(luaL_testudata(L, idx, "lupi.sprite_ref"));
    return ud ? ud->sheet.get() : nullptr;
}

// ---------------------------------------------------------------------------
// screen control
// ---------------------------------------------------------------------------

static int l_ui_cls(lua_State* L)
{
    uint8_t color = (uint8_t)optint(L,1,1);
    lupi_draw_cls(*lupi_self(L), color);
    return 0;
}

static int l_ui_clip(lua_State* L)
{
    lupi_p* p = lupi_self(L);
    if (lua_gettop(L) == 0) {
        p->gfx.reset_clip();
    } else {
        int x = checkint(L,1);
        int y = checkint(L,2);
        int w = checkint(L,3);
        int h = checkint(L,4);
        p->gfx.clip_x0 = x; p->gfx.clip_y0 = y;
        p->gfx.clip_x1 = x + w; p->gfx.clip_y1 = y + h;
    }
    return 0;
}

static int l_ui_camera(lua_State* L)
{
    lupi_p* p = lupi_self(L);
    if (lua_gettop(L) == 0) {
        p->gfx.camera_x = 0; p->gfx.camera_y = 0;
    } else {
        p->gfx.camera_x = checkint(L,1);
        p->gfx.camera_y = checkint(L,2);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// primitives
// ---------------------------------------------------------------------------

static int l_ui_rect(lua_State* L)
{
    lupi_draw_rect(*lupi_self(L),
        checkint(L,1), checkint(L,2),
        checkint(L,3), checkint(L,4),
        (uint8_t)checkint(L,5));
    return 0;
}

static int l_ui_rectfill(lua_State* L)
{
    lupi_draw_rectfill(*lupi_self(L),
        checkint(L,1), checkint(L,2),
        checkint(L,3), checkint(L,4),
        (uint8_t)checkint(L,5));
    return 0;
}

// ui.draw_rect(x, y, width, height, filled, color) — a width/height rect,
// unlike ui.rect/ui.rectfill's two-corner form.
static int l_ui_draw_rect(lua_State* L)
{
    int x = checkint(L,1), y = checkint(L,2), w = checkint(L,3), h = checkint(L,4);
    bool filled = lua_toboolean(L, 5);
    uint8_t color = (uint8_t)checkint(L,6);
    if (filled) lupi_draw_rectfill(*lupi_self(L), x, y, x + w, y + h, color);
    else        lupi_draw_rect(*lupi_self(L), x, y, x + w, y + h, color);
    return 0;
}

static int l_ui_circ(lua_State* L)
{
    lupi_draw_circ(*lupi_self(L),
        checkint(L,1), checkint(L,2),
        checkint(L,3), (uint8_t)checkint(L,4));
    return 0;
}

static int l_ui_circfill(lua_State* L)
{
    lupi_draw_circfill(*lupi_self(L),
        checkint(L,1), checkint(L,2),
        checkint(L,3), (uint8_t)checkint(L,4));
    return 0;
}

static int l_ui_line(lua_State* L)
{
    lupi_draw_line(*lupi_self(L),
        checkint(L,1), checkint(L,2),
        checkint(L,3), checkint(L,4),
        (uint8_t)checkint(L,5));
    return 0;
}

static int l_ui_trisfill(lua_State* L)
{
    lupi_draw_trisfill(*lupi_self(L),
        checkint(L,1), checkint(L,2),
        checkint(L,3), checkint(L,4),
        checkint(L,5), checkint(L,6),
        (uint8_t)checkint(L,7));
    return 0;
}

static int l_ui_grid(lua_State* L)
{
    int x  = checkint(L,1);
    int y  = checkint(L,2);
    int cw = checkint(L,3);
    int ch = checkint(L,4);
    luaL_checktype(L, 5, LUA_TTABLE);

    lupi_p* p = lupi_self(L);
    lua_Integer nrows = (lua_Integer)lua_rawlen(L, 5);
    for (lua_Integer row = 1; row <= nrows; ++row) {
        lua_rawgeti(L, 5, row);
        if (lua_istable(L, -1)) {
            lua_Integer ncols = (lua_Integer)lua_rawlen(L, -1);
            for (lua_Integer col = 1; col <= ncols; ++col) {
                lua_rawgeti(L, -1, col);
                if (!lua_isnil(L, -1) && lua_toboolean(L, -1)) {
                    int color = (int)lua_tointeger(L, -1);
                    int px = x + (int)(col - 1) * cw;
                    int py = y + (int)(row - 1) * ch;
                    lupi_draw_rectfill(*p, px, py, px + cw - 1, py + ch - 1, (uint8_t)color);
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }
    return 0;
}

// ui.spr(sheet, x, y, flipped) — a single optional bool, horizontal flip
// only (matches the real API exactly; any further trailing args some real
// cart code passes are simply unused, same as the reference does).
static int l_ui_spr(lua_State* L)
{
    lupi_spritesheet* sheet = lupi_check_sprite_ref(L, 1);
    if (!sheet) return luaL_argerror(L, 1, "expected a sprite_ref");
    int x = checkint(L,2);
    int y = checkint(L,3);
    bool flipped = lua_toboolean(L, 4);
    lupi_draw_tile(*lupi_self(L), *sheet, 0, x, y, flipped, false);
    return 0;
}

// ui.tile(sheet, tile_index, x, y) — the flip flag is baked into tile_index
// itself (bit 1024 = horizontal flip; there is no vertical-flip bit for this
// call, unlike ui.map's per-cell tile ids which also honor 2048), not passed
// as separate arguments.
static int l_ui_tile(lua_State* L)
{
    lupi_spritesheet* sheet = lupi_check_sprite_ref(L, 1);
    if (!sheet) return luaL_argerror(L, 1, "expected a sprite_ref");
    int tile_index_with_flags = checkint(L,2);
    int x = checkint(L,3);
    int y = checkint(L,4);
    bool flipped = (tile_index_with_flags & 1024) != 0;
    int tile_id = tile_index_with_flags & ~1024;
    lupi_draw_tile(*lupi_self(L), *sheet, tile_id, x, y, flipped, false);
    return 0;
}

static int l_ui_map(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    int cam_x = optint(L,2,0);
    int cam_y = optint(L,3,0);

    // Real compiled shape (confirmed against a real cart's map.lua/director.lua/
    // hud.lua, which index e.g. map.POI.pois[i] directly) — reproduced by our
    // own Tiled-JSON compiler (tiled_maps.cpp), NOT a newbase-invented format:
    //   { metadata = {width,height,tile_size},
    //     tilesets = {[tileset_name] = image_stem, ...},
    //     LAYER = {[tileset_name] = {[1-based cell index] = tile_id, ...}, ...},
    //     ... }
    // A single layer can span multiple tilesets at once (e.g. FG1 spans both
    // W1SA and W1SB) — see doc/system_lupi.md's "ui.map data contract".
    lua_getfield(L, 1, "metadata");
    luaL_checktype(L, -1, LUA_TTABLE);
    lua_getfield(L, -1, "width");     int width     = optint(L,-1,0); lua_pop(L, 1);
    lua_getfield(L, -1, "height");    int height    = optint(L,-1,0); lua_pop(L, 1);
    lua_getfield(L, -1, "tile_size"); int tile_size = optint(L,-1,8); lua_pop(L, 1);
    lua_pop(L, 1); // metadata

    lua_getfield(L, 1, "tilesets");
    luaL_checktype(L, -1, LUA_TTABLE);
    int tilesets_idx = lua_gettop(L);

    lupi_p* p = lupi_self(L);

    lua_pushnil(L); // first key for the tilesets-dict iteration
    while (lua_next(L, tilesets_idx) != 0) {
        // stack: tilesets_idx ... key(tileset name), value(image stem)
        const char* tileset_name = lua_tostring(L, -2);
        const char* image_stem   = lua_tostring(L, -1);

        const std::string image_name = image_stem ? image_stem : "";
        auto it = p->sprite_by_name.find(image_name);
        if (it == p->sprite_by_name.end()) {
            // Map data may contain the source path (for example,
            // "../gfx/basictiles"), while sprites are registered by basename.
            const size_t separator = image_name.find_last_of("/\\");
            const std::string base_name = image_name.substr(
                separator == std::string::npos ? 0 : separator + 1);
            it = p->sprite_by_name.find(base_name);
        }
        if (it == p->sprite_by_name.end()) {
            log::warn("[lupi] ui.map: sprite '%s' (tileset '%s') not found", image_stem, tileset_name);
            lua_pop(L, 1); // drop value, keep key for lua_next
            continue;
        }
        lupi_spritesheet& sheet = *it->second;

        lua_getfield(L, 1, tileset_name); // this layer's sparse {[cell]=tile_id} for this tileset, or nil
        if (lua_istable(L, -1)) {
            int sub_idx = lua_gettop(L);
            lua_pushnil(L);
            while (lua_next(L, sub_idx) != 0) {
                int cell_index = (int)lua_tointeger(L, -2); // 1-based, row-major
                int raw = (int)lua_tointeger(L, -1);
                lua_pop(L, 1); // keep key for lua_next

                // low bits = local tile id, +1024/+2048 = h/v flip — see
                // tiled_maps.cpp's tiled_id_to_lupi_id.
                int local_id = raw & 0x3FF;
                bool flip_x = (raw & 1024) != 0;
                bool flip_y = (raw & 2048) != 0;

                int i0 = cell_index - 1;
                int row = i0 / width, col = i0 % width;
                lupi_draw_tile(*p, sheet, local_id,
                    col * tile_size + cam_x, row * tile_size + cam_y, flip_x, flip_y);
            }
        }
        lua_pop(L, 1); // drop this tileset's sub-table (or nil)
        lua_pop(L, 1); // drop value, keep key for lua_next
    }
    return 0;
}

static int l_ui_print(lua_State* L)
{
    const char* text = luaL_checkstring(L, 1);
    int x = checkint(L,2);
    int y = checkint(L,3);
    uint8_t color = (uint8_t)checkint(L,4);
    lupi_draw_print(*lupi_self(L), text, x, y, color);
    return 0;
}

// ---------------------------------------------------------------------------
// palette / patterns / utility
// ---------------------------------------------------------------------------

static int l_ui_palset(lua_State* L)
{
    int idx = checkint(L,1);
    uint16_t color = (uint16_t)checkint(L,2);
    lupi_self(L)->pal.set(idx, color);
    return 0;
}

static int l_ui_fillp(lua_State* L)
{
    lupi_p* p = lupi_self(L);
    if (lua_gettop(L) == 0) {
        for (auto& b : p->gfx.fillp) b = 0;
    } else {
        for (int i = 0; i < 8; ++i)
            p->gfx.fillp[i] = (uint8_t)optint(L,i + 1,0);
    }
    return 0;
}

static int l_ui_mid(lua_State* L)
{
    double a = luaL_checknumber(L, 1);
    double b = luaL_checknumber(L, 2);
    double c = luaL_checknumber(L, 3);
    double lo = std::min(a, b), hi = std::max(a, b);
    lua_pushnumber(L, std::max(lo, std::min(hi, c)));
    return 1;
}

static int l_ui_stat(lua_State* L)
{
    int opt = checkint(L,1);
    lupi_p* p = lupi_self(L);
    double val = 0.0;
    switch (opt) {
        case 0: val = (double)lua_gc(L, LUA_GCCOUNT, 0) * 1024.0; break; // approx bytes
        case 1: val = p->cpu_ema; break;
        case 7: val = p->fps_ema; break;
        default: val = 0.0; break;
    }
    lua_pushnumber(L, val);
    return 1;
}

// ---------------------------------------------------------------------------
// registration
// ---------------------------------------------------------------------------

static const luaL_Reg k_ui_funcs[] = {
    {"cls", l_ui_cls}, {"clip", l_ui_clip}, {"camera", l_ui_camera},
    {"rect", l_ui_rect}, {"rectfill", l_ui_rectfill}, {"draw_rect", l_ui_draw_rect},
    {"circ", l_ui_circ}, {"circfill", l_ui_circfill},
    {"line", l_ui_line}, {"trisfill", l_ui_trisfill}, {"grid", l_ui_grid},
    {"spr", l_ui_spr}, {"tile", l_ui_tile}, {"map", l_ui_map},
    {"print", l_ui_print},
    {"palset", l_ui_palset}, {"fillp", l_ui_fillp},
    {"mid", l_ui_mid}, {"stat", l_ui_stat},
    {nullptr, nullptr}
};

struct const_def { const char* name; int value; };
static const const_def k_consts[] = {
    {"LEFT",0}, {"RIGHT",1}, {"UP",2}, {"DOWN",3},
    {"BTN_Z",4}, {"BTN_X",5},
    {"BTN_F",12}, {"BTN_G",13}, {"BTN_Q",14}, {"BTN_E",15},
};

void nb::lupi_register_ui(lua_State* L)
{
    luaL_newmetatable(L, "lupi.sprite_ref");
    lua_pushcfunction(L, l_sprite_ref_gc);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);

    lua_newtable(L);
    luaL_setfuncs(L, k_ui_funcs, 0);
    lua_setglobal(L, "ui");

    for (const auto& c : k_consts) {
        lua_pushinteger(L, c.value);
        lua_setglobal(L, c.name);
    }
}
