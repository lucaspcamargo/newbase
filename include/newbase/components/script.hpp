#pragma once

#include <newbase/res/script.hpp>
#include <newbase/script_lua/lua.hpp>
#include <memory>

namespace nb {
    struct cscript {
        std::shared_ptr<rscript> script {};
        lua_State *state {nullptr};
        bool ready {false};
        bool skip {false};
        int env_ref { LUA_NOREF };  // luaL_ref to this entity's Lua environment table

        static void _ensure_rtti();
    };
}
