#pragma once

#include <newbase/script_lua/lua.hpp>
#include <newbase/utility/meta_callback.hpp>
#include <entt/entt.hpp>
#include <memory>
#include <functional>

namespace nb {
namespace lua {


    
// Ref-counted handle to a Lua function stored in the registry.
// Copyable (shared ownership); registry ref is released when the last copy dies.
struct lua_function {
    struct _ref {
        _ref(lua_State *lua_state, int _iref) : L(lua_state), ref(_iref)
        {
        }
        lua_State *L;
        int ref;
        ~_ref() { luaL_unref(L, LUA_REGISTRYINDEX, ref); }
    };
    std::shared_ptr<_ref> _s;

    lua_function() = default;
    // Expects a Lua function on top of the stack; pops it.
    explicit lua_function(lua_State *L)
        : _s(std::make_shared<_ref>(L, luaL_ref(L, LUA_REGISTRYINDEX))) {}

    bool valid() const { return _s != nullptr; }
};

// Register the lua_function entt meta type.
// Call once during Lua state init (no lua_State needed — meta is global).
void register_lua_function_type();

// Build a meta_callback that dispatches through a Lua function.
meta_callback make_meta_callback_from_lua(const lua_function &lf);

// If arg holds a lua_function and expected is meta_callback, coerce it.
// Otherwise returns arg unchanged.
entt::meta_any coerce_lua_function_arg(entt::meta_any arg, entt::meta_type expected);

// Registry key for the box metatable
constexpr const char *BOX_METATABLE = "nb.meta_any";

// Lua userdata box: holds a type-erased value (owning or non-owning) plus
// an optional shared_ptr for lifetime management (used for systems).
struct lua_nb_box {
    entt::meta_any value;
    std::shared_ptr<void> owner;
};

// Register the nb.meta_any metatable. Call once during Lua state init.
void register_box_metatable(lua_State *L);

// Push a new box onto the Lua stack.
// value: the meta_any to wrap (may be owning or a reference via forward_as_meta)
// owner: optional shared_ptr to keep an external object alive (e.g. systems)
void push_meta_any(lua_State *L, entt::meta_any value, std::shared_ptr<void> owner = {});

// Convert a Lua stack value at idx to a meta_any.
// Returns an empty meta_any if the type is unsupported.
entt::meta_any lua_to_meta_any(lua_State *L, int idx);

} // namespace lua
} // namespace nb
