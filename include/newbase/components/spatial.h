#pragma once

#include <glm/vec3.hpp>
#include <glm/mat3x3.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
            world = parent * glm::translate(
                glm::rotate( 
                    glm::rotate( 
                        glm::rotate(
                            glm::scale(glm::mat4x4{1.f}, scale)
                            , glm::radians(rot[0]), glm::vec3{1.f,0.f,0.f})
                        , glm::radians(rot[1]), glm::vec3{0.f,1.f,0.f})
                    , glm::radians(rot[2]), glm::vec3{0.f,0.f,1.f}
                ), pos
            );
        }
    };


}