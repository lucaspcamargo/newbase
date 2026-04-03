#pragma once

#include <newbase/res/resource.hpp>
#include <newbase/res/texture.hpp>
#include <entt/entt.hpp>
#include <glm/vec2.hpp>
#include <memory>

namespace nb {

struct rsprite : public resource {
    explicit rsprite(entt::id_type id = 0) : resource(id, entt::hashed_string{"rsprite"}.value()) {}

    // Cached texture — kept alive as long as the sprite is referenced.
    std::shared_ptr<rtexture> tex {};

    glm::vec2 anchor {.5f, .5f};
    glm::vec2 dims   {-1.0f, -1.0f}; // -1 means use full texture size
};

}
