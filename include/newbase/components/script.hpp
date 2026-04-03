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

        static void _ensure_rtti();
    };
}
