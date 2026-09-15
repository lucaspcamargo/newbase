#include "lupi_internal.hpp"

using namespace nb;

static bool valid_id(int id, int player)
{
    return id >= 0 && id < LUPI_BUTTON_COUNT && player >= 0 && player < LUPI_MAX_PLAYERS;
}

static int l_ui_btn(lua_State* L)
{
    int id = (int)luaL_checkinteger(L, 1);
    int player = (int)luaL_optinteger(L, 2, 0);
    lupi_p* p = lupi_self(L);
    if (!valid_id(id, player)) { lua_pushboolean(L, 0); return 1; }
    uint8_t pressure = p->btn.pressure_this_frame[player][id];
    if (pressure == 0) lua_pushboolean(L, 0);
    else lua_pushinteger(L, pressure);
    return 1;
}

static int l_ui_btnp(lua_State* L)
{
    int id = (int)luaL_checkinteger(L, 1);
    int player = (int)luaL_optinteger(L, 2, 0);
    lupi_p* p = lupi_self(L);
    if (!valid_id(id, player)) { lua_pushboolean(L, 0); return 1; }
    uint8_t pressure = p->btn.pressure_this_frame[player][id];
    if (p->btn.pending_pressed[player][id]) lua_pushinteger(L, pressure != 0 ? pressure : 255);
    else lua_pushboolean(L, 0);
    return 1;
}

static int l_ui_mouse(lua_State* L)
{
    lupi_p* p = lupi_self(L);
    lua_pushnumber(L, p->mouse.x);
    lua_pushnumber(L, p->mouse.y);
    lua_pushinteger(L, p->mouse.buttons);
    lua_pushinteger(L, 0); // wheel_x — unsupported per spec
    lua_pushinteger(L, 0); // wheel_y — unsupported per spec
    return 5;
}

static int l_ui_peektext(lua_State* L)
{
    lupi_p* p = lupi_self(L);
    char c;
    if (p->text.peek(c)) {
        lua_pushlstring(L, &c, 1);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int l_ui_readtext(lua_State* L)
{
    lupi_p* p = lupi_self(L);
    char c;
    if (p->text.read(c)) {
        lua_pushlstring(L, &c, 1);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static const luaL_Reg k_input_funcs[] = {
    {"btn", l_ui_btn}, {"btnp", l_ui_btnp}, {"mouse", l_ui_mouse},
    {"peektext", l_ui_peektext}, {"readtext", l_ui_readtext},
    {nullptr, nullptr}
};

void nb::lupi_register_input(lua_State* L)
{
    lua_getglobal(L, "ui");
    luaL_setfuncs(L, k_input_funcs, 0);
    lua_pop(L, 1);
}
