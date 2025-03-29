#pragma once

#include <newbase/res/script.h>
#include <entt/resource/resource.hpp>
#include <newbase/script_lua/lua.h>

namespace nb {
    struct cscript {
        entt::resource<rscript> script {};
        lua_State *state {nullptr};
        bool ready {false};
        bool skip {false};
    };
}
