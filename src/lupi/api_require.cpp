#include "lupi_internal.hpp"
#include <newbase/res/manager.hpp>
#include <newbase/res/script.hpp>
#include <newbase/log.hpp>
#include <algorithm>
#include <string>

using namespace nb;

// ---------------------------------------------------------------------------
// Real carts require() modules in three shapes lupi-codec produces at compile
// time — "sprites"/"palette" (compiler-generated, no source file), "maps.X"
// (compiled from Tiled JSON), and ordinary sibling .lua files (pass-through,
// untouched by the compiler). We don't have the compiler, so this replaces
// Lua's own file-based require() outright rather than trying to satisfy it
// through package.path/preload — see lupi_internal.hpp for the rationale.
// ---------------------------------------------------------------------------

namespace {

// Loads and runs cart_dir/<name>.lua as a plain chunk (side-effect style —
// every real sibling module here defines globals, none `return`s a value).
// Leaves either the chunk's return value or an error object on top of the
// stack; returns false in the error case.
bool load_and_run_sibling(lua_State* L, lupi_p& p, const std::string& name)
{
    // Dots are directory separators for nested modules, matching Lua's own
    // dotted-module convention — confirmed necessary against real cart code
    // (e.g. require "player.player" -> player/player.lua, require
    // "player.states.idle" -> player/states/idle.lua).
    std::string rel = name;
    std::replace(rel.begin(), rel.end(), '.', '/');
    std::string path = p.cart_dir + rel + ".lua";
    auto id = entt::hashed_string{path.c_str()}.value();
    auto script = rman().get<rscript>(id);
    if (!script || !script->valid) {
        lua_pushfstring(L, "module '%s' not found (looked for %s)", name.c_str(), path.c_str());
        return false;
    }
    std::string src = lupi_preprocess_binary_literals(std::string(script->raw.begin(), script->raw.end()));
    if (luaL_loadbuffer(L, src.data(), src.size(), script->chunkname.c_str()) != LUA_OK)
        return false;
    if (lua_pcall(L, 0, 1, 0) != LUA_OK)
        return false;
    return true;
}

int l_require(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    auto* p = static_cast<lupi_p*>(lua_touserdata(L, lua_upvalueindex(1)));

    luaL_getsubtable(L, LUA_REGISTRYINDEX, "lupi_loaded"); // cache table, mirrors package.loaded
    lua_getfield(L, -1, name);
    if (!lua_isnil(L, -1)) {
        lua_remove(L, -2); // drop cache table, keep cached value
        return 1;
    }
    lua_pop(L, 1); // pop nil

    std::string n = name;
    if (n == "sprites" || n == "palette") {
        // Already populated as globals by lupi_scan_cart_assets before
        // game.lua runs — nothing left to do.
        lua_pushboolean(L, true);
    } else if (n.rfind("maps.", 0) == 0) {
        lua_getfield(L, LUA_REGISTRYINDEX, "lupi_maps");
        lua_getfield(L, -1, name);
        lua_remove(L, -2);
        if (lua_isnil(L, -1))
            return luaL_error(L, "require '%s': no compiled map found (expected maps/%s.json)",
                               name, name + 5);
    } else {
        if (!load_and_run_sibling(L, *p, n))
            return lua_error(L); // error object already on top of the stack
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_pushboolean(L, true); // vanilla require(): a module returning nothing caches as `true`
        }
    }

    lua_pushvalue(L, -1);
    lua_setfield(L, -3, name); // lupi_loaded[name] = result (cache table is still at -3... see below)
    return 1;
}

}

void nb::lupi_register_require(lua_State* L, lupi_p& p)
{
    lua_pushlightuserdata(L, &p);
    lua_pushcclosure(L, l_require, 1);
    lua_setglobal(L, "require");
}
