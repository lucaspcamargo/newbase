#include <newbase/script_lua/bindings_glm.h>
#include <glm/glm.hpp>

void nb::_lua_bind_glm(sol::state_view sv)
{
    // vec3
    {
        auto vec3_mult_overloads = sol::overload(
            [](const glm::vec3& v1, const glm::vec3& v2) -> glm::vec3 { return v1*v2; },
            [](const glm::vec3& v1, float f) -> glm::vec3 { return v1*f; },
            [](float f, const glm::vec3& v1) -> glm::vec3 { return f*v1; }
        );

        auto vec3_add_overloads = sol::overload(
            [](const glm::vec3& v1, const glm::vec3& v2) -> glm::vec3 { return v1+v2; },
            [](const glm::vec3& v1, float f) -> glm::vec3 { return v1+f; },
            [](float f, const glm::vec3& v1) -> glm::vec3 { return f+v1; }
        );

        auto vec3 = sv.new_usertype<glm::vec3>("vec3",
            sol::constructors<glm::vec3(), glm::vec3(float), glm::vec3(float, float, float)>(),
            "x", &glm::vec3::x,
            "y", &glm::vec3::y,
            "z", &glm::vec3::z,
            sol::meta_function::multiplication, vec3_mult_overloads,
            sol::meta_function::addition, vec3_add_overloads);

        auto vec2 = sv.new_usertype<glm::vec2>("vec2",
            sol::constructors<glm::vec2(), glm::vec2(float), glm::vec2(float, float)>(),
            "x", &glm::vec2::x,
            "y", &glm::vec2::y,
            sol::meta_function::multiplication, sol::overload(
                [](const glm::vec2& v1, const glm::vec2& v2) -> glm::vec2 { return v1*v2; },
                [](const glm::vec2& v1, float f) -> glm::vec2 { return v1*f; },
                [](float f, const glm::vec2& v1) -> glm::vec2 { return f*v1; }
            ),
            sol::meta_function::addition, sol::overload(
                [](const glm::vec2& v1, const glm::vec2& v2) -> glm::vec2 { return v1+v2; },
                [](const glm::vec2& v1, float f) -> glm::vec2 { return v1+f; },
                [](float f, const glm::vec2& v1) -> glm::vec2 { return f+v1; }
            )
        );
    }
}