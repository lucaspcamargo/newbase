#pragma once

// lua inclusion header for the lupi system
// helps us make sure it is always included with the correct linkage or appropriate defines
// mirrors newbase/script_lua/lua.hpp — lupi does NOT share a lua_State with script_lua,
// but both link against the same vendored Lua library target.

extern "C" {
    #include <lua.h>
    #include <lualib.h>
    #include <lauxlib.h>
}
