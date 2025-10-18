#pragma once

#include <newbase/res/script.hpp>
#include <newbase/script_lua/lua.hpp>
#include <entt/resource/resource.hpp>

namespace nb {
    struct cscript {
        entt::resource<rscript> script {};
        lua_State *state {nullptr};
        bool ready {false};
        bool skip {false};

        static void _ensure_rtti();
    };
}
