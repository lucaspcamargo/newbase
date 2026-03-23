#pragma once

#include <newbase/res/resource.hpp>
#include <vector>
#include <string>

namespace nb {

enum class script_type {
    LUA_SOURCE,
    LUA_BYTECODE
};

struct rscript : public resource {
    explicit rscript(entt::id_type id = 0) : resource(id, entt::hashed_string{"rscript"}.value()) {}

    bool valid {false};
    script_type type {script_type::LUA_SOURCE};
    std::vector<char> raw {};
    std::string chunkname;
};

}
