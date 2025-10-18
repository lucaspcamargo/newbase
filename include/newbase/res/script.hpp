#pragma once

extern "C" {
#include <lua.h>
}
#include <vector>
#include <string>

namespace nb {

enum class script_type {
    LUA_SOURCE,
    LUA_BYTECODE
};

struct rscript {
    bool valid;
    script_type type {script_type::LUA_SOURCE};
    std::vector<char> raw {};
    std::string chunkname;  // TODO find out from resource manager in loader?
};

}
