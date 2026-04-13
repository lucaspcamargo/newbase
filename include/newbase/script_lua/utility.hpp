#pragma once

#include <newbase/log.hpp>
#include <newbase/script_lua/lua.hpp>

namespace nb {
    namespace lua {

        class stack_guard {
        public:
            stack_guard(lua_State *L) : _lua(L), _top(lua_gettop(L)) {}
            ~stack_guard() {
                if(lua_gettop(_lua) != _top)
                {
                    log::error("[script_lua] Lua stack imbalance detected: before=%d, after=%d", _top, lua_gettop(_lua));
                    lua_settop(_lua, _top);
                }
            }
        private:
            lua_State *_lua;
            int _top;
        };

    }
}