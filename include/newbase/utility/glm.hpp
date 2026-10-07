#pragma once

// GLM - OpenGL Mathematics
// not only includes what we use most of the time, but also contains our usage onfiguration

#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include <glm/glm.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>  // remember, construct with wxyz argument order
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
