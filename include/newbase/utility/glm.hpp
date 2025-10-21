#pragma once

// GLM - OpenGL Mathematics
// This header not only includes the GLM functionality we use,
// but also defines some template specializations to allow usage
// with some standard library constructs used for bindings,
// such as std::is_nothrow_copy_constructible, std::is_nothrow_copy_assignable, etc.

// if not using these constructs with GLM types, nothing stops us from including GLM directly :)

#include <glm/glm.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>  // remember, construct with wxyz argument order
#include <type_traits>

// std type_traits specializations
// this was not resolving the sol2 issues on clang (Android NDK 46)
/*
namespace std {
    template<>
    struct is_nothrow_copy_constructible<glm::vec2> : std::true_type {};
    template<>
    struct is_nothrow_copy_constructible<glm::vec3> : std::true_type {};
    template<>
    struct is_nothrow_copy_constructible<glm::vec4> : std::true_type {};
    template<>
    struct is_nothrow_copy_constructible<glm::mat4> : std::true_type {};
    template<>
    struct is_nothrow_copy_constructible<glm::quat> : std::true_type {};

    template<>
    struct is_nothrow_copy_assignable<glm::vec2> : std::true_type {};
    template<>
    struct is_nothrow_copy_assignable<glm::vec3> : std::true_type {};
    template<>
    struct is_nothrow_copy_assignable<glm::vec4> : std::true_type {};
    template<>
    struct is_nothrow_copy_assignable<glm::mat4> : std::true_type {};
    template<>
    struct is_nothrow_copy_assignable<glm::quat> : std::true_type {};
}
    */