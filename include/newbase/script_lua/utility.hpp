#pragma once

#include <newbase/log.hpp>
#include <newbase/script_lua/lua.hpp>

namespace nb {
    namespace lua {

        class stack_guard {
        public:
            stack_guard(lua_State *L) : _L(L), _top(lua_gettop(L)) {}
            ~stack_guard() {
                if(lua_gettop(_L) != _top)
                {
                    log::error("[script_lua] Lua stack imbalance detected: before=%d, after=%d", _top, lua_gettop(_L));
                    lua_settop(_L, _top);
                }
            }
        private:
            lua_State *_L;
            int _top;
        };

    }
}