#pragma once

#include <glm/vec3.hpp>
#include <glm/mat3x3.hpp>

namespace nb {
    struct cspatial {
        glm::mat3x3 basis;
        glm::vec3 pos;

        inline void clear()
        {
            basis = glm::mat3x3{1.0f};
            pos = glm::vec3{};
        }
    };
}