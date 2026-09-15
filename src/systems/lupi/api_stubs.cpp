#include "lupi_internal.hpp"
#include <newbase/log.hpp>
#include <string>

using namespace nb;

// Routes plain Lua print() (e.g. cart debug prints) through the engine's own
// logging rather than stdout, so it shows up alongside every other [lupi]
// log line. Joins arguments with a tab and stringifies each the same way
// Lua's own base print() would (luaL_tolstring honors any __tostring
// metamethod), matching print()'s usual behavior.
static int l_print_override(lua_State* L)
{
    int n = lua_gettop(L);
    std::string joined;
    for (int i = 1; i <= n; ++i) {
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        if (i > 1) joined += '\t';
        joined.append(s, len);
        lua_pop(L, 1); // pop the string luaL_tolstring pushed
    }
    log::info("[lupi_print] %s", joined.c_str());
    return 0;
}

void nb::lupi_register_print(lua_State* L)
{
    lua_pushcfunction(L, l_print_override);
    lua_setglobal(L, "print");
}

// Deferred functionality (see plan's "Deferred work" section): both sfx.* and
// the Clay-shaped layout builder API are registered as harmless no-ops here so
// cart scripts that reference them don't crash the whole cart via pcall.
// Real implementations are follow-up work, not built in this pass.

// TODO(lupi-audio): wire to nb::audio via entt::locator, see src/audio/audio.cpp.
// Cart manifests will need a parallel sounds:/music: asset section alongside assets:.
static int l_sfx_music(lua_State* L)  { return 0; }
static int l_sfx_fx(lua_State* L)     { return 0; }
static int l_sfx_volume(lua_State* L) { return 0; }

static const luaL_Reg k_sfx_funcs[] = {
    {"music", l_sfx_music}, {"fx", l_sfx_fx}, {"volume", l_sfx_volume},
    {nullptr, nullptr}
};

// TODO(lupi-clay): once the separate, general-purpose "clay" UI system exists
// (vendored/clay + bindings), replace these with a thin adapter translating
// this builder-call tree into that system's Clay bindings, reading back
// render commands into the draw.cpp primitives already built for lupi.
static int l_ui_layout_stub(lua_State* L) { return 0; }

static int l_box_stub(lua_State* L)    { lua_newtable(L); return 1; }
static int l_text_stub(lua_State* L)   { lua_newtable(L); return 1; }
static int l_image_stub(lua_State* L)  { lua_newtable(L); return 1; }
static int l_custom_stub(lua_State* L) { lua_newtable(L); return 1; }

void nb::lupi_register_stubs(lua_State* L)
{
    lua_newtable(L);
    luaL_setfuncs(L, k_sfx_funcs, 0);
    lua_setglobal(L, "sfx");

    lua_getglobal(L, "ui");
    lua_pushcfunction(L, l_ui_layout_stub);
    lua_setfield(L, -2, "layout");
    lua_pop(L, 1);

    lua_pushcfunction(L, l_box_stub);    lua_setglobal(L, "Box");
    lua_pushcfunction(L, l_text_stub);   lua_setglobal(L, "Text");
    lua_pushcfunction(L, l_image_stub);  lua_setglobal(L, "Image");
    lua_pushcfunction(L, l_custom_stub); lua_setglobal(L, "Custom");
}
