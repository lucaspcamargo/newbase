#pragma once

#include <newbase/res/resource.hpp>
#include <newbase/res/texture.hpp>
#include <entt/entt.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <string>
#include <vector>

namespace nb {

struct sprite_frame {
    glm::vec4 source_rect { 0.f, 0.f, -1.f, -1.f }; // x, y, w, h; -1 w/h = full texture
    float     duration    { 0.1f };
};

struct sprite_sequence {
    std::string              name;
    bool                     loop { true };
    std::string              next; // sequence to play after this one ends (empty = stay/stop)
    std::vector<sprite_frame> frames;
};

struct rsprite : public resource {
    explicit rsprite(entt::id_type id = 0) : resource(id, entt::hashed_string{"rsprite"}.value()) {}

    std::shared_ptr<rtexture>     tex    {};
    glm::vec2                     anchor { .5f,  .5f  };
    glm::vec2                     dims   { -1.f, -1.f }; // -1 = use texture size

    std::vector<sprite_sequence>  sequences;

    const sprite_sequence* find_sequence(const std::string& name) const
    {
        for (const auto& s : sequences)
            if (s.name == name) return &s;
        return nullptr;
    }
    const sprite_sequence* first_sequence() const
    {
        return sequences.empty() ? nullptr : &sequences[0];
    }
};

}
