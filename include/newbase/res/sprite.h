#pragma once

#include <entt/entt.hpp>
#include <glm/vec2.hpp>

namespace nb {

struct rsprite {
    entt::id_type id_tex{ entt::null };
    glm::vec2 anchor{ .5f, .5f };
    glm::vec2 dims{ -1.0f, -1.0f }; // default dims mean use from texture
};

}
