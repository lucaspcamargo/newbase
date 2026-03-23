#include <newbase/reflection/lib_glm.hpp>
#include <newbase/reflection/data.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <entt/meta/factory.hpp>

using entt::operator""_hs;

// vec2 operators
static glm::vec2 vec2_add(const glm::vec2& a, const glm::vec2& b) { return a + b; }
static glm::vec2 vec2_sub(const glm::vec2& a, const glm::vec2& b) { return a - b; }
static glm::vec2 vec2_mul(const glm::vec2& a, const glm::vec2& b) { return a * b; }
static glm::vec2 vec2_div(const glm::vec2& a, const glm::vec2& b) { return a / b; }
static glm::vec2 vec2_mul_f(const glm::vec2& a, float f)          { return a * f; }
static glm::vec2 vec2_div_f(const glm::vec2& a, float f)          { return a / f; }
static glm::vec2 vec2_unm(const glm::vec2& a)                     { return -a; }
static bool      vec2_eq(const glm::vec2& a, const glm::vec2& b)  { return a == b; }

// vec3 operators
static glm::vec3 vec3_add(const glm::vec3& a, const glm::vec3& b) { return a + b; }
static glm::vec3 vec3_sub(const glm::vec3& a, const glm::vec3& b) { return a - b; }
static glm::vec3 vec3_mul(const glm::vec3& a, const glm::vec3& b) { return a * b; }
static glm::vec3 vec3_div(const glm::vec3& a, const glm::vec3& b) { return a / b; }
static glm::vec3 vec3_mul_f(const glm::vec3& a, float f)          { return a * f; }
static glm::vec3 vec3_div_f(const glm::vec3& a, float f)          { return a / f; }
static glm::vec3 vec3_unm(const glm::vec3& a)                     { return -a; }
static bool      vec3_eq(const glm::vec3& a, const glm::vec3& b)  { return a == b; }

// vec4 operators
static glm::vec4 vec4_add(const glm::vec4& a, const glm::vec4& b) { return a + b; }
static glm::vec4 vec4_sub(const glm::vec4& a, const glm::vec4& b) { return a - b; }
static glm::vec4 vec4_mul(const glm::vec4& a, const glm::vec4& b) { return a * b; }
static glm::vec4 vec4_div(const glm::vec4& a, const glm::vec4& b) { return a / b; }
static glm::vec4 vec4_mul_f(const glm::vec4& a, float f)          { return a * f; }
static glm::vec4 vec4_div_f(const glm::vec4& a, float f)          { return a / f; }
static glm::vec4 vec4_unm(const glm::vec4& a)                     { return -a; }
static bool      vec4_eq(const glm::vec4& a, const glm::vec4& b)  { return a == b; }

namespace nb::rtti {

    void register_lib_glm()
    {
        entt::meta_factory<glm::vec2>{}
            .type("glm_vec2"_hs)
            .custom<type_info>(type_info{.identifier="vec2", .type_class=TYPE_CLASS_NONE})
            .ctor<>()
            .ctor<float>()
            .ctor<float, float>()
            .data<&glm::vec2::x>("x"_hs)
            .data<&glm::vec2::y>("y"_hs)
            .func<&vec2_add>("op_add"_hs)
            .func<&vec2_sub>("op_sub"_hs)
            .func<&vec2_mul>("op_mul"_hs)
            .func<&vec2_div>("op_div"_hs)
            .func<&vec2_mul_f>("op_mul_f"_hs)
            .func<&vec2_div_f>("op_div_f"_hs)
            .func<&vec2_unm>("op_unm"_hs)
            .func<&vec2_eq>("op_eq"_hs);

        entt::meta_factory<glm::vec3>{}
            .type("glm_vec3"_hs)
            .custom<type_info>(type_info{.identifier="vec3", .type_class=TYPE_CLASS_NONE})
            .ctor<>()
            .ctor<float>()
            .ctor<float, float, float>()
            .data<&glm::vec3::x>("x"_hs)
            .data<&glm::vec3::y>("y"_hs)
            .data<&glm::vec3::z>("z"_hs)
            .func<&vec3_add>("op_add"_hs)
            .func<&vec3_sub>("op_sub"_hs)
            .func<&vec3_mul>("op_mul"_hs)
            .func<&vec3_div>("op_div"_hs)
            .func<&vec3_mul_f>("op_mul_f"_hs)
            .func<&vec3_div_f>("op_div_f"_hs)
            .func<&vec3_unm>("op_unm"_hs)
            .func<&vec3_eq>("op_eq"_hs);

        entt::meta_factory<glm::vec4>{}
            .type("glm_vec4"_hs)
            .custom<type_info>(type_info{.identifier="vec4", .type_class=TYPE_CLASS_NONE})
            .ctor<>()
            .ctor<float>()
            .ctor<float, float, float, float>()
            .data<&glm::vec4::x>("x"_hs)
            .data<&glm::vec4::y>("y"_hs)
            .data<&glm::vec4::z>("z"_hs)
            .data<&glm::vec4::w>("w"_hs)
            .data<&glm::vec4::r>("r"_hs)
            .data<&glm::vec4::g>("g"_hs)
            .data<&glm::vec4::b>("b"_hs)
            .data<&glm::vec4::a>("a"_hs)
            .func<&vec4_add>("op_add"_hs)
            .func<&vec4_sub>("op_sub"_hs)
            .func<&vec4_mul>("op_mul"_hs)
            .func<&vec4_div>("op_div"_hs)
            .func<&vec4_mul_f>("op_mul_f"_hs)
            .func<&vec4_div_f>("op_div_f"_hs)
            .func<&vec4_unm>("op_unm"_hs)
            .func<&vec4_eq>("op_eq"_hs);

        entt::meta_factory<glm::quat>{}
            .type("glm_quat"_hs)
            .custom<type_info>(type_info{.identifier="quat", .type_class=TYPE_CLASS_NONE})
            .ctor<>()
            .ctor<float, float, float, float>()
            .data<&glm::quat::w>("w"_hs)
            .data<&glm::quat::x>("x"_hs)
            .data<&glm::quat::y>("y"_hs)
            .data<&glm::quat::z>("z"_hs);
        
    }
}