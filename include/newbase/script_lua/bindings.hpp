#pragma once

#include <newbase/script_lua/lua.hpp>
#include <entt/entt.hpp>
#include <memory>

namespace nb {
namespace lua {


    
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
