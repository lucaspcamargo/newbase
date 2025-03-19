#pragma once

#include <lua.h>
#include <vector>

namespace nb {

enum class script_type {
    LUA_SOURCE,
    LUA_BYTECODE
};

enum class script_state {
    UNPARSED_SOURCE,    // raw source, without compilation
    BYTECODE,           // source parsed into bytecode
    READY               // ready for execution
};

struct rscript {
    bool valid;
    script_type type {script_type::LUA_SOURCE};
    script_state state {script_state::UNPARSED_SOURCE};
    std::vector<char> raw {};
};

}
