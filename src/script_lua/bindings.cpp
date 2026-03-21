#include <newbase/script_lua/bindings.hpp>
#include <newbase/log.hpp>
#include <string>
#include <vector>

using namespace nb;
using namespace nb::lua;

// forward declarations of metamethods
static int box_gc(lua_State *L);
static int box_index(lua_State *L);
static int box_newindex(lua_State *L);
static int box_tostring(lua_State *L);
static int box_func_call(lua_State *L);

void nb::lua::register_box_metatable(lua_State *L)
{
    luaL_newmetatable(L, BOX_METATABLE);

    lua_pushcfunction(L, box_gc);
    lua_setfield(L, -2, "__gc");

    lua_pushcfunction(L, box_index);
    lua_setfield(L, -2, "__index");

    lua_pushcfunction(L, box_newindex);
    lua_setfield(L, -2, "__newindex");

    lua_pushcfunction(L, box_tostring);
    lua_setfield(L, -2, "__tostring");

    lua_pop(L, 1);
}

void nb::lua::push_meta_any(lua_State *L, entt::meta_any value, std::shared_ptr<void> owner)
{
    auto *box = static_cast<lua_nb_box*>(lua_newuserdata(L, sizeof(lua_nb_box)));
    new (box) lua_nb_box { std::move(value), std::move(owner) };
    luaL_getmetatable(L, BOX_METATABLE);
    lua_setmetatable(L, -2);
}

entt::meta_any nb::lua::lua_to_meta_any(lua_State *L, int idx)
{
    switch(lua_type(L, idx))
    {
    case LUA_TBOOLEAN:
        return entt::meta_any{ (bool)lua_toboolean(L, idx) };
    case LUA_TNUMBER:
        if(lua_isinteger(L, idx))
            return entt::meta_any{ (lua_Integer)lua_tointeger(L, idx) };
        else
            return entt::meta_any{ (double)lua_tonumber(L, idx) };
    case LUA_TSTRING:
        return entt::meta_any{ std::string{lua_tostring(L, idx)} };
    case LUA_TUSERDATA:
        if(luaL_testudata(L, idx, BOX_METATABLE))
        {
            auto *box = static_cast<lua_nb_box*>(lua_touserdata(L, idx));
            return box->value.as_ref();
        }
        return {};
    default:
        return {};
    }
}

static int box_gc(lua_State *L)
{
    auto *box = static_cast<lua_nb_box*>(lua_touserdata(L, 1));
    box->~lua_nb_box();
    return 0;
}

static int box_index(lua_State *L)
{
    auto *box = static_cast<lua_nb_box*>(luaL_checkudata(L, 1, BOX_METATABLE));

    if(!box->value || lua_type(L, 2) != LUA_TSTRING)
    {
        lua_pushnil(L);
        return 1;
    }

    const char *key = lua_tostring(L, 2);
    auto hash = entt::hashed_string{key}.value();
    auto type = box->value.type();

    // data member access
    if(auto d = type.data(hash); d)
    {
        auto result = d.get(box->value);
        if(result)
        {
            push_meta_any(L, std::move(result));
            return 1;
        }
    }

    // method access — push a closure capturing the userdata (not a raw pointer,
    // so the GC cannot collect the box while the closure is alive)
    if(auto f = type.func(hash); f)
    {
        lua_pushvalue(L, 1);                          // upvalue 1: the box userdata
        lua_pushinteger(L, (lua_Integer)hash);         // upvalue 2: function hash
        lua_pushcclosure(L, box_func_call, 2);
        return 1;
    }

    lua_pushnil(L);
    return 1;
}

static int box_newindex(lua_State *L)
{
    auto *box = static_cast<lua_nb_box*>(luaL_checkudata(L, 1, BOX_METATABLE));

    if(!box->value || lua_type(L, 2) != LUA_TSTRING)
        return 0;

    const char *key = lua_tostring(L, 2);
    auto hash = entt::hashed_string{key}.value();
    auto type = box->value.type();

    if(auto d = type.data(hash); d)
    {
        auto val = lua_to_meta_any(L, 3);
        if(val)
            d.set(box->value, std::move(val));
        else
            log::warn("[bindings] __newindex: unsupported value type for key '%s'", key);
    }

    return 0;
}

static int box_tostring(lua_State *L)
{
    auto *box = static_cast<lua_nb_box*>(luaL_checkudata(L, 1, BOX_METATABLE));

    if(!box->value)
    {
        lua_pushstring(L, "nb.meta_any (empty)");
        return 1;
    }

    auto name = box->value.type().info().name();
    lua_pushfstring(L, "nb.meta_any (%.*s)", (int)name.size(), name.data());
    return 1;
}

static int box_func_call(lua_State *L)
{
    auto *box = static_cast<lua_nb_box*>(lua_touserdata(L, lua_upvalueindex(1)));
    auto hash = (entt::id_type)lua_tointeger(L, lua_upvalueindex(2));

    auto func = box->value.type().func(hash);
    if(!func)
        return 0;

    // When called with method syntax (obj:method(a, b)), Lua passes obj as
    // stack[1]. Detect this and skip it so we don't double-pass the instance.
    int arg_start = 1;
    if(lua_gettop(L) >= 1 && luaL_testudata(L, 1, BOX_METATABLE))
    {
        if(static_cast<lua_nb_box*>(lua_touserdata(L, 1)) == box)
            arg_start = 2;
    }

    int argc = lua_gettop(L) - (arg_start - 1);
    std::vector<entt::meta_any> args;
    args.reserve(argc);
    for(int i = arg_start; i <= lua_gettop(L); ++i)
        args.push_back(lua_to_meta_any(L, i));

    auto result = func.invoke(box->value, args.empty() ? nullptr : args.data(), args.size());
    if(result)
    {
        push_meta_any(L, std::move(result));
        return 1;
    }

    return 0;
}
