#pragma once

#include <newbase/res/resource.hpp>
#include <entt/entt.hpp>
#include <glm/vec2.hpp>

namespace nb {

struct rsprite : public resource {
    explicit rsprite(entt::id_type id = 0) : resource(id, entt::hashed_string{"rsprite"}.value()) {}

    entt::id_type id_tex {entt::null};
    glm::vec2 anchor {.5f, .5f};
    glm::vec2 dims {-1.0f, -1.0f}; // default dims mean use from texture
};

}
