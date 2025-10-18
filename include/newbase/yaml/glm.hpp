#pragma once

#include <glm/vec3.hpp>
#include <glm/mat3x3.hpp>
#include <ryml.hpp>


namespace nb {

    bool load_vec3(ryml::ConstNodeRef in, glm::vec3 &dst);

}