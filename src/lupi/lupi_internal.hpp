#pragma once

// Internal helpers shared across src/lupi/*.cpp. Not part of the public API.

#include <newbase/lupi/lupi_p.hpp>
#include <memory>
#include <vector>

namespace nb {

// Fetches the owning lupi_p* stashed in the Lua registry at init() time.
// Every lua_CFunction binding starts by calling this to reach engine-side state.
lupi_p* lupi_self(lua_State* L);

// --- drawing primitives (draw.cpp) ---------------------------------------

// Applies camera offset, clip-rect test, and (for fill ops) the fillp dither
// test, then writes the pixel if all pass. Every primitive routes through
// this so clip/camera/fillp are applied identically everywhere.
void lupi_put_pixel(lupi_p& p, int x, int y, uint8_t color, bool is_fill_op);

void lupi_draw_cls(lupi_p& p, uint8_t color);
void lupi_draw_rect(lupi_p& p, int x0, int y0, int x1, int y1, uint8_t color);
void lupi_draw_rectfill(lupi_p& p, int x0, int y0, int x1, int y1, uint8_t color);
void lupi_draw_circ(lupi_p& p, int cx, int cy, int r, uint8_t color);
void lupi_draw_circfill(lupi_p& p, int cx, int cy, int r, uint8_t color);
void lupi_draw_line(lupi_p& p, int x0, int y0, int x1, int y1, uint8_t color);
void lupi_draw_trisfill(lupi_p& p, int x1, int y1, int x2, int y2, int x3, int y3, uint8_t color);
void lupi_draw_print(lupi_p& p, const char* text, int x, int y, uint8_t color);

// Blits tile_size^2 pixels from sheet's tile_id at (x,y). Palette index 0 in
// sheet-space is treated as transparent. Not a fill op (fillp does not apply).
void lupi_draw_tile(lupi_p& p, const lupi_spritesheet& sheet, int tile_id, int x, int y,
                     bool flip_x, bool flip_y);

// --- Lua registration (api_ui.cpp / api_input.cpp / api_stubs.cpp) -------

void lupi_register_ui(lua_State* L);      // ui.* drawing/stat/fillp/mid + Lua globals for constants
void lupi_register_input(lua_State* L);   // ui.btn/btnp/mouse/peektext/readtext
void lupi_register_io(lua_State* L, lupi_p& p); // read-only io.open backed by rman()
void lupi_register_stubs(lua_State* L);   // sfx.* and ui.layout()/Box()/Text()/Image()/Custom() no-ops
void lupi_register_print(lua_State* L);   // overrides global print() to route through nb::log::info

// --- sprite_ref userdata (api_ui.cpp) -------------------------------------

void lupi_push_sprite_ref(lua_State* L, std::shared_ptr<lupi_spritesheet> sheet);
lupi_spritesheet* lupi_check_sprite_ref(lua_State* L, int idx);

// --- spritesheet loading (loaders.cpp) ------------------------------------

// Decodes PNG bytes and auto-palettizes into `pal` (allocating new palette
// entries as needed, falling back to nearest-BGR555 match once full). Tile
// size/count are auto-detected from the real lupi-codec's magic-marker
// convention (see MAGIC_COLOR_BGR555 in loaders.cpp) rather than declared —
// real carts have no manifest field for it. Called directly at cart-boot
// time rather than through the generic rman() cache, since the result
// depends on the cart's live, shared palette state.
std::shared_ptr<lupi_spritesheet> lupi_load_spritesheet_indexed(
    const std::vector<char>& png_bytes, lupi_palette& pal, const std::string& path = {});

// Loads a cart resource (lupi.yaml manifest + sibling game.lua via rscript) by resource id.
struct rloader_lupi_cart {
    using result_type = std::shared_ptr<rlupi_cart>;
    result_type operator()(entt::id_type id) const;
};

// Real Lupi carts use `0b1010...` binary integer literals (e.g. ui.fillp bit
// patterns) — standard Lua (5.5 included) has no such literal syntax, only
// decimal/0x. Rather than patch the vendored Lua lexer, rewrite them to plain
// decimal literals before compiling, skipping over string/long-string/comment
// spans so we don't touch anything that merely looks like "0b101" in text.
std::string lupi_preprocess_binary_literals(const std::string& src);

// Recursively discovers every .png under p.cart_dir's img/+maps/ subtrees
// (via rman()'s resource index — see scripts/res_indexer.py), palettizes
// each into p.pal, and populates both p.sprite_by_name and the Lua globals
// `Sprites`/`Palette` a real cart's game.lua expects (Sprites.find(name),
// Palette[i]) — our own runtime substitute for lupi-codec's compiler output.
void lupi_scan_cart_assets(lua_State* L, lupi_p& p);

// Applies an optional per-cart palette_overrides.yaml sidecar (newbase-only
// metadata, not part of the real Lupi format — see doc/system_lupi.md's
// "Palette overrides" section). No-op if the cart has none. Called from
// lupi_scan_cart_assets, after the normal auto-scan and before the Lua
// `Palette` global is built, so overrides land in it too.
void lupi_apply_palette_overrides(lupi_p& p);

// --- require() (api_require.cpp) ------------------------------------------

// Replaces the global `require` with one that understands a cart's own
// module layout: "sprites"/"palette" are no-ops (already populated as
// globals before game.lua runs — see lupi_scan_cart_assets), "maps.<name>"
// resolves to a precompiled Tiled map table (see lupi_compile_cart_maps),
// and anything else resolves to a sibling "<name>.lua" file under p.cart_dir,
// executed once and cached — mirroring vanilla require() semantics without
// depending on Lua's file-based package.path (keeps module resolution
// resource-system-aware, portable across storage backends).
void lupi_register_require(lua_State* L, lupi_p& p);

// --- Tiled JSON map compiler (tiled_maps.cpp) ------------------------------

// Compiles every maps/*.json under p.cart_dir (Tiled's JSON map export) into
// the exact Lua table shape lupi-codec's tiled_codec.lua would have produced
// — {metadata={width,height,tile_size}, tilesets={name=image_stem,...},
// LAYER={tileset_name={[cell_index]=tile_id,...}, metadata=.., tilesets=..}}
// — and stashes each under a private Lua registry table keyed by "maps.<name>"
// for lupi_register_require's "maps.*" case to return directly. See
// doc/system_lupi.md's "ui.map data contract" for the full shape and why.
void lupi_compile_cart_maps(lua_State* L, lupi_p& p);

}
