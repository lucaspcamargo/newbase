#include "lupi_internal.hpp"
#include <newbase/res/manager.hpp>
#include <newbase/log.hpp>
#include <ryml_std.hpp>
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

using namespace nb;

// ---------------------------------------------------------------------------
// Reimplements lupi-codec's tiled_codec.lua/tiled_processor.lua/tiled_generator.lua
// entirely in C++: we don't have the Lupi platform's own compiler, and real
// carts ship raw Tiled JSON (maps/*.json) with no compiled maps/*.lua sitting
// next to it — see doc/system_lupi.md's "ui.map data contract" for the
// confirmed real output shape this reproduces (verified against a real
// cart's map.lua / director.lua / hud.lua, which index `map.POI.pois[i]`
// etc — so this shape is NOT free to redesign, it's what the real game code
// already expects).
// ---------------------------------------------------------------------------

namespace {

struct tileset_info {
    std::string name;
    std::string image_stem; // .png stripped
    int firstgid { 1 };
};

// Tiled's top 3 GID bits are flip flags; the real bottom 29 bits are the id.
constexpr uint32_t TILED_FLIP_H  = 0x80000000u;
constexpr uint32_t TILED_FLIP_V  = 0x40000000u;
constexpr uint32_t TILED_ID_MASK = 0x1FFFFFFFu;

// Re-encodes a Tiled per-layer local id (gid - tileset.firstgid, flip bits
// still intact from the raw gid) into lupi-codec's own numeric convention:
// low bits = local tile id, +1024 = h-flip, +2048 = v-flip. Diagonal flip
// (0x20000000) is dropped — lupi-codec doesn't encode it either, and no real
// cart map uses it (confirmed empirically).
int tiled_id_to_lupi_id(uint32_t local_raw)
{
    bool flip_h = (local_raw & TILED_FLIP_H) != 0;
    bool flip_v = (local_raw & TILED_FLIP_V) != 0;
    int tile_id = (int)(local_raw & TILED_ID_MASK);
    return tile_id + (flip_h ? 1024 : 0) + (flip_v ? 2048 : 0);
}

// Which tileset a gid belongs to: lupi-codec masks the (still flip-bit-laden)
// gid down to its low 10 bits before comparing against firstgid — replicated
// verbatim (see tiled_processor.lua's find_tileset_for_gid) since real carts'
// firstgid/local-id ranges all stay well under 1024, making this equivalent
// to a full flip-strip for every map we've seen, but we match the spec
// exactly rather than the coincidence.
const tileset_info* find_tileset_for_gid(uint32_t gid, const std::vector<tileset_info>& sorted_desc)
{
    uint32_t gid_base = gid & 0x3FFu;
    for (const auto& ts : sorted_desc)
        if (gid_base >= (uint32_t)ts.firstgid)
            return &ts;
    return nullptr;
}

bool has_suffix(const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// The real compiler runs over a cart's *entire* file tree and picks a codec
// per file by sniffing content, not by folder location (see lupi-codec's
// codecs.lua/tiled_validator.lua: any "*.json" gets this same check) — so a
// Tiled map can live anywhere in a cart (confirmed: a real cart ships one at
// "sprites/bgmap.json", required as "sprites.bgmap", not under a "maps/"
// folder at all). Replicates the same tiledversion+type=="map" sniff so an
// unrelated .json (a save file, config, etc) isn't mistaken for a map.
bool looks_like_tiled_map(const std::vector<char>& json_bytes)
{
    auto data = json_bytes; // ryml parses in place
    auto tree = ryml::parse_json_in_place(c4::to_substr(data.data()));
    auto root = tree.rootref();
    if (!root.is_map() || !root.has_child("tiledversion") || !root.has_child("type"))
        return false;
    std::string type;
    root["type"] >> type;
    return type == "map";
}

std::string strip_png(const std::string& s)
{
    return has_suffix(s, ".png") ? s.substr(0, s.size() - 4) : s;
}

// layer name -> (tileset name -> (1-based cell index -> lupi tile id))
using layer_tiles_t = std::map<std::string, std::map<int, int>>;

struct compiled_layer {
    std::string name;
    layer_tiles_t by_tileset;
};

struct compiled_map {
    int width { 0 }, height { 0 }, tile_size { 0 };
    std::vector<tileset_info> tilesets;
    std::vector<compiled_layer> layers;
};

// Mirrors lupi-codec's tiled_validator.lua: the real compiler hard-rejects a
// map at build time on any of these, refusing to ship it at all. We can't
// refuse to ship (the cart's already running), so instead of silently
// misparsing something we don't understand — e.g. treating a base64-encoded
// layer's `data` string as an int array — we log the same rejection loudly
// and skip just that map, leaving a clear "module not found" for the cart's
// own require() to surface instead of garbage tiles.
bool validate_tiled_map(ryml::ConstNodeRef root, const std::string& path)
{
    auto str_is = [](ryml::ConstNodeRef n, const char* expect) {
        std::string s; n >> s; return s == expect;
    };

    if (root.has_child("orientation") && !str_is(root["orientation"], "orthogonal")) {
        log::error("[tiled_maps] %s: only 'orthogonal' orientation is supported", path.c_str());
        return false;
    }
    if (root.has_child("renderorder") && !str_is(root["renderorder"], "right-down")) {
        log::error("[tiled_maps] %s: only 'right-down' render order is supported", path.c_str());
        return false;
    }
    if (root.has_child("infinite")) {
        bool infinite = false; root["infinite"] >> infinite;
        if (infinite) {
            log::error("[tiled_maps] %s: infinite maps are not supported", path.c_str());
            return false;
        }
    }
    if (root.has_child("layers")) {
        for (auto layer_node : root["layers"]) {
            if (layer_node.has_child("type") && !str_is(layer_node["type"], "tilelayer"))
                continue; // no objectgroups in real carts, but be safe

            std::string name;
            if (layer_node.has_child("name")) layer_node["name"] >> name;

            if (name == "metadata" || name == "tilesets") {
                log::error("[tiled_maps] %s: layer name '%s' is reserved", path.c_str(), name.c_str());
                return false;
            }
            int lx = 0, ly = 0;
            if (layer_node.has_child("x")) layer_node["x"] >> lx;
            if (layer_node.has_child("y")) layer_node["y"] >> ly;
            if (lx != 0 || ly != 0) {
                log::error("[tiled_maps] %s: layer '%s' must start at x=0,y=0", path.c_str(), name.c_str());
                return false;
            }
            if (layer_node.has_child("data") && !layer_node["data"].is_seq()) {
                log::error("[tiled_maps] %s: layer '%s' data isn't a plain array — export Tiled layer "
                           "data as CSV, not Base64/compressed", path.c_str(), name.c_str());
                return false;
            }
        }
    }
    return true;
}

bool compile_one(const std::vector<char>& json_bytes, const std::string& path, compiled_map& m)
{
    auto data = json_bytes; // ryml parses in place
    auto tree = ryml::parse_json_in_place(c4::to_substr(data.data()));
    auto root = tree.rootref();

    if (!validate_tiled_map(root, path))
        return false;

    if (root.has_child("width"))     root["width"]     >> m.width;
    if (root.has_child("height"))    root["height"]    >> m.height;
    if (root.has_child("tilewidth")) root["tilewidth"] >> m.tile_size;

    if (root.has_child("tilesets")) {
        for (auto ts_node : root["tilesets"]) {
            tileset_info ts;
            if (ts_node.has_child("name")) { std::string s; ts_node["name"] >> s; ts.name = s; }
            if (ts_node.has_child("image")) { std::string s; ts_node["image"] >> s; ts.image_stem = strip_png(s); }
            if (ts_node.has_child("firstgid")) ts_node["firstgid"] >> ts.firstgid;
            m.tilesets.push_back(std::move(ts));
        }
    }
    std::vector<tileset_info> sorted_desc = m.tilesets;
    std::sort(sorted_desc.begin(), sorted_desc.end(),
              [](const tileset_info& a, const tileset_info& b) { return a.firstgid > b.firstgid; });

    if (root.has_child("layers")) {
        for (auto layer_node : root["layers"]) {
            if (layer_node.has_child("type")) {
                std::string type; layer_node["type"] >> type;
                if (type != "tilelayer") continue; // no objectgroups in real carts, but be safe
            }
            compiled_layer layer;
            if (layer_node.has_child("name")) { std::string s; layer_node["name"] >> s; layer.name = s; }
            if (!layer_node.has_child("data")) continue;

            int i = 1; // 1-based cell index, matches Tiled/Lua array convention
            for (auto v : layer_node["data"]) {
                uint32_t gid = 0;
                v >> gid;
                if (gid > 0) {
                    const tileset_info* ts = find_tileset_for_gid(gid, sorted_desc);
                    if (ts) {
                        uint32_t local_raw = gid - (uint32_t)ts->firstgid;
                        layer.by_tileset[ts->name][i] = tiled_id_to_lupi_id(local_raw);
                    } else {
                        log::warn("[tiled_maps] %s: gid %u in layer '%s' matches no tileset",
                                  path.c_str(), gid, layer.name.c_str());
                    }
                }
                ++i;
            }
            m.layers.push_back(std::move(layer));
        }
    }
    return true;
}

// Pushes a fresh Lua table {[k1]=v1, [k2]=v2, ...} for a sparse int->int map.
void push_sparse_int_table(lua_State* L, const std::map<int, int>& sparse)
{
    lua_newtable(L);
    for (const auto& [idx, tile_id] : sparse) {
        lua_pushinteger(L, tile_id);
        lua_rawseti(L, -2, idx);
    }
}

// Pushes the full compiled map as a Lua table, matching the shape documented
// in doc/system_lupi.md ("ui.map data contract").
void push_compiled_map(lua_State* L, const compiled_map& m)
{
    lua_newtable(L); // M

    lua_newtable(L); // metadata
    lua_pushinteger(L, m.width);     lua_setfield(L, -2, "width");
    lua_pushinteger(L, m.height);    lua_setfield(L, -2, "height");
    lua_pushinteger(L, m.tile_size); lua_setfield(L, -2, "tile_size");
    lua_setfield(L, -2, "metadata"); // M.metadata = metadata (pops it)

    lua_newtable(L); // tilesets
    for (const auto& ts : m.tilesets) {
        lua_pushstring(L, ts.image_stem.c_str());
        lua_setfield(L, -2, ts.name.c_str());
    }
    lua_setfield(L, -2, "tilesets"); // M.tilesets = tilesets (pops it)

    for (const auto& layer : m.layers) {
        lua_newtable(L); // LAYER
        for (const auto& [tileset_name, sparse] : layer.by_tileset) {
            push_sparse_int_table(L, sparse);
            lua_setfield(L, -2, tileset_name.c_str());
        }
        lua_getfield(L, -2, "metadata"); lua_setfield(L, -2, "metadata"); // LAYER.metadata = M.metadata
        lua_getfield(L, -2, "tilesets"); lua_setfield(L, -2, "tilesets"); // LAYER.tilesets = M.tilesets
        lua_setfield(L, -2, layer.name.c_str()); // M[layer.name] = LAYER
    }
    // M left on top of the stack for the caller.
}

}

void nb::lupi_compile_cart_maps(lua_State* L, lupi_p& p)
{
    lua_newtable(L); // the "lupi_maps" registry table: "<dotted.module.path>" -> compiled table
    int n = 0;
    for (const auto& [res_id, handle] : rman().handles()) {
        if (handle.path.compare(0, p.cart_dir.size(), p.cart_dir) != 0) continue;
        if (!has_suffix(handle.path, ".json")) continue;

        std::vector<char> bytes;
        if (!rman().read_all_sync(res_id, bytes, true)) {
            log::error("[lupi_compile_cart_maps] cannot read '%s'", handle.path.c_str());
            continue;
        }
        if (!looks_like_tiled_map(bytes)) continue; // not every .json in a cart is a map

        compiled_map m;
        if (!compile_one(bytes, handle.path, m)) continue; // rejected — see validate_tiled_map

        // Module name mirrors require()'s own dot-for-slash convention (see
        // api_require.cpp) applied to the path relative to the cart root, so
        // e.g. "maps/stages/w1s1.json" registers as "maps.stages.w1s1" —
        // confirmed against a real cart's own require("maps.stages.w1s1")
        // and require("maps.world.m") call sites, not just a flat filename
        // stem (which only happens to work when maps/ has no subfolders).
        std::string rel = handle.path.substr(p.cart_dir.size());
        rel = rel.substr(0, rel.size() - 5); // strip ".json"
        std::replace(rel.begin(), rel.end(), '/', '.');

        push_compiled_map(L, m);
        lua_setfield(L, -2, rel.c_str());
        ++n;
    }
    lua_setfield(L, LUA_REGISTRYINDEX, "lupi_maps");
    log::info("[lupi_compile_cart_maps] compiled %d map(s) under '%s'", n, p.cart_dir.c_str());
}
