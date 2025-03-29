#pragma once

// lua inclusion header
// helps us make sure it is always included with the correct linkage or appropriate defines

extern "C" {
    #include <lua.h>
    #include <lualib.h>
    #include <lauxlib.h>
}