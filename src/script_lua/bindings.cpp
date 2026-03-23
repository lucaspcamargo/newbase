#include <newbase/script_lua/bindings.hpp>
#include <newbase/log.hpp>
#include <entt/meta/factory.hpp>
#include <string>
#include <vector>
#include <functional>

using entt::operator""_hs;

using namespace nb;
using namespace nb::lua;

// forward declarations of metamethods
static int box_gc(lua_State *L);
static int box_index(lua_State *L);
static int box_newindex(lua_State *L);
static int box_tostring(lua_State *L);
static int box_func_call(lua_State *L);

// helper: invoke a named binary op on lhs (box op box)
static int box_arith_vv(lua_State *L, entt::id_type op_hash)
{
    auto *a = static_cast<lua_nb_box*>(luaL_testudata(L, 1, BOX_METATABLE));
    auto *b = static_cast<lua_nb_box*>(luaL_testudata(L, 2, BOX_METATABLE));
    if (!a || !a->value || !b || !b->value) { lua_pushnil(L); return 1; }
    auto func = a->value.type().func(op_hash);
    if (!func) { lua_pushnil(L); return 1; }
    auto bref = b->value.as_ref();
    auto result = func.invoke(a->value, &bref, 1);
    if (result) { push_meta_any(L, std::move(result)); return 1; }
    lua_pushnil(L);
    return 1;
}

// helper: invoke op_mul_f / op_div_f (box op scalar, or scalar op box for mul)
static int box_arith_vf(lua_State *L, entt::id_type op_hash)
{
    lua_nb_box *box = nullptr;
    float scalar = 0.f;
    if (luaL_testudata(L, 1, BOX_METATABLE) && lua_isnumber(L, 2))
    {
        box    = static_cast<lua_nb_box*>(lua_touserdata(L, 1));
        scalar = static_cast<float>(lua_tonumber(L, 2));
    }
    else if (lua_isnumber(L, 1) && luaL_testudata(L, 2, BOX_METATABLE))
    {
        box    = static_cast<lua_nb_box*>(lua_touserdata(L, 2));
        scalar = static_cast<float>(lua_tonumber(L, 1));
    }
    if (!box || !box->value) { lua_pushnil(L); return 1; }
    auto func = box->value.type().func(op_hash);
    if (!func) { lua_pushnil(L); return 1; }
    entt::meta_any farg { scalar };
    auto result = func.invoke(box->value, &farg, 1);
    if (result) { push_meta_any(L, std::move(result)); return 1; }
    lua_pushnil(L);
    return 1;
}

static int box_add(lua_State *L) { return box_arith_vv(L, "op_add"_hs); }
static int box_sub(lua_State *L) { return box_arith_vv(L, "op_sub"_hs); }
static int box_div(lua_State *L)
{
    // prefer vec/vec, fall back to vec/scalar
    if (luaL_testudata(L, 1, BOX_METATABLE) && luaL_testudata(L, 2, BOX_METATABLE))
        return box_arith_vv(L, "op_div"_hs);
    return box_arith_vf(L, "op_div_f"_hs);
}
static int box_mul(lua_State *L)
{
    // prefer vec*vec, fall back to vec*scalar (or scalar*vec)
    if (luaL_testudata(L, 1, BOX_METATABLE) && luaL_testudata(L, 2, BOX_METATABLE))
        return box_arith_vv(L, "op_mul"_hs);
    return box_arith_vf(L, "op_mul_f"_hs);
}
static int box_unm(lua_State *L)
{
    auto *a = static_cast<lua_nb_box*>(luaL_testudata(L, 1, BOX_METATABLE));
    if (!a || !a->value) { lua_pushnil(L); return 1; }
    auto func = a->value.type().func("op_unm"_hs);
    if (!func) { lua_pushnil(L); return 1; }
    auto result = func.invoke(a->value, nullptr, 0);
    if (result) { push_meta_any(L, std::move(result)); return 1; }
    lua_pushnil(L);
    return 1;
}
static int box_eq(lua_State *L)
{
    auto *a = static_cast<lua_nb_box*>(luaL_testudata(L, 1, BOX_METATABLE));
    auto *b = static_cast<lua_nb_box*>(luaL_testudata(L, 2, BOX_METATABLE));
    if (!a || !b || !a->value || !b->value) { lua_pushboolean(L, false); return 1; }
    auto func = a->value.type().func("op_eq"_hs);
    if (!func) { lua_pushboolean(L, false); return 1; }
    auto bref = b->value.as_ref();
    auto result = func.invoke(a->value, &bref, 1);
    if (result)
    {
        auto *v = result.try_cast<bool>();
        lua_pushboolean(L, v && *v);
        return 1;
    }
    lua_pushboolean(L, false);
    return 1;
}

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

    lua_pushcfunction(L, box_add);
    lua_setfield(L, -2, "__add");
    lua_pushcfunction(L, box_sub);
    lua_setfield(L, -2, "__sub");
    lua_pushcfunction(L, box_mul);
    lua_setfield(L, -2, "__mul");
    lua_pushcfunction(L, box_div);
    lua_setfield(L, -2, "__div");
    lua_pushcfunction(L, box_unm);
    lua_setfield(L, -2, "__unm");
    lua_pushcfunction(L, box_eq);
    lua_setfield(L, -2, "__eq");

    lua_pop(L, 1);
}

void nb::lua::push_meta_any(lua_State *L, entt::meta_any value, std::shared_ptr<void> owner)
{
    if (!value) { lua_pushnil(L); return; }

    // unbox primitive types directly onto the Lua stack — use exact type id to
    // avoid matching complex types that happen to be coercible to bool/int/etc.
    auto ti = value.type().info();
    if      (ti == entt::type_id<bool>())          { lua_pushboolean(L, *value.try_cast<bool>());         return; }
    else if (ti == entt::type_id<float>())         { lua_pushnumber(L,  *value.try_cast<float>());        return; }
    else if (ti == entt::type_id<double>())        { lua_pushnumber(L,  *value.try_cast<double>());       return; }
    else if (ti == entt::type_id<int>())           { lua_pushinteger(L, *value.try_cast<int>());          return; }
    else if (ti == entt::type_id<unsigned int>())  { lua_pushinteger(L, *value.try_cast<unsigned int>()); return; }
    else if (ti == entt::type_id<lua_Integer>())   { lua_pushinteger(L, *value.try_cast<lua_Integer>());  return; }
    else if (ti == entt::type_id<entt::entity>())  { lua_pushinteger(L, entt::to_integral(*value.try_cast<entt::entity>())); return; }
    else if (ti == entt::type_id<std::string>())   { lua_pushstring(L,  value.try_cast<std::string>()->c_str()); return; }

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
    case LUA_TFUNCTION:
        lua_pushvalue(L, idx);           // copy to top — luaL_ref pops it
        return entt::meta_any{ lua_function{L} };
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

    auto name = std::string{box->value.type().info().name()};
    lua_pushfstring(L, "nb.meta_any (%s)", name.c_str());
    return 1;
}

static std::function<void()> conv_lua_fn_to_void(const lua_function &f)
{
    return [s = f._s]() {
        lua_rawgeti(s->L, LUA_REGISTRYINDEX, s->ref);
        if(lua_pcall(s->L, 0, 0, 0) != LUA_OK)
        {
            log::error("[lua_function] call error: %s", lua_tostring(s->L, -1));
            lua_pop(s->L, 1);
        }
    };
}

static std::function<void(float)> conv_lua_fn_to_void_float(const lua_function &f)
{
    return [s = f._s](float v) {
        lua_rawgeti(s->L, LUA_REGISTRYINDEX, s->ref);
        lua_pushnumber(s->L, v);
        if(lua_pcall(s->L, 1, 0, 0) != LUA_OK)
        {
            log::error("[lua_function] call error: %s", lua_tostring(s->L, -1));
            lua_pop(s->L, 1);
        }
    };
}

void nb::lua::register_lua_function_type()
{
    entt::meta_factory<lua_function>{}
        .type("lua_function"_hs)
        .conv<&conv_lua_fn_to_void>()
        .conv<&conv_lua_fn_to_void_float>();
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
