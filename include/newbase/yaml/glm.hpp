#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat3x3.hpp>
#include <ryml.hpp>

namespace nb {

    bool load_vec2(ryml::ConstNodeRef in, glm::vec2 &dst);
    bool load_vec3(ryml::ConstNodeRef in, glm::vec3 &dst);
    bool load_vec4(ryml::ConstNodeRef in, glm::vec4 &dst);

    // Safe variants: check validity and child count before loading.
    // Return false (and leave dst unchanged) if the node is invalid or has wrong shape.
    bool try_load_float(ryml::ConstNodeRef in, float &dst);
    bool try_load_bool (ryml::ConstNodeRef in, bool  &dst);
    bool try_load_vec2 (ryml::ConstNodeRef in, glm::vec2 &dst);
    bool try_load_vec4 (ryml::ConstNodeRef in, glm::vec4 &dst);

}