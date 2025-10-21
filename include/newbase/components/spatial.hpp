#pragma once

#include <newbase/utility/glm.hpp>


namespace nb {
    struct cspatial {
        glm::vec3 pos;
        glm::vec3 rot;
        glm::vec3 scale;

        glm::mat4x4 world;
        
        inline void clear()
        {
            pos = glm::vec3{};
            rot = glm::vec3{};
            scale = glm::vec3{1.0f};
            world = glm::mat4x4{1.0f};
        }

        // TODO move to spatial subsystem? With dirty flag or something?
        inline void apply(const glm::mat4x4 parent = glm::mat4x4{1.0f})
        {
            world = parent;
            world = glm::translate(world, pos);
            world = glm::rotate(world, glm::radians(rot[0]), glm::vec3{1.f,0.f,0.f});
            world = glm::rotate(world, glm::radians(rot[1]), glm::vec3{0.f,1.f,0.f});
            world = glm::rotate(world, glm::radians(rot[2]), glm::vec3{0.f,0.f,1.f});
            world = glm::scale(world, scale);
        }

        static void _ensure_rtti();
    };


}