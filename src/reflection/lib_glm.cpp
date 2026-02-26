#include <newbase/reflection/lib_glm.hpp>
#include <newbase/reflection/data.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <entt/meta/factory.hpp>

using entt::operator""_hs;

namespace nb::rtti {

    void register_lib_glm()
    {
        entt::meta_factory<glm::vec2>{}
            .type("glm_vec2"_hs)
            .custom<type_info>(type_info{.identifier="vec2", .type_class=TYPE_CLASS_NONE})
            .data<&glm::vec2::x>("x"_hs)
            .data<&glm::vec2::y>("y"_hs);

        entt::meta_factory<glm::vec3>{}
            .type("glm_vec3"_hs)
            .custom<type_info>(type_info{.identifier="vec3", .type_class=TYPE_CLASS_NONE})
            .data<&glm::vec3::x>("x"_hs)
            .data<&glm::vec3::y>("y"_hs)
            .data<&glm::vec3::z>("z"_hs);

        entt::meta_factory<glm::vec4>{}
            .type("glm_vec4"_hs)
            .custom<type_info>(type_info{.identifier="vec4", .type_class=TYPE_CLASS_NONE})
            .data<&glm::vec4::x>("x"_hs)
            .data<&glm::vec4::y>("y"_hs)
            .data<&glm::vec4::z>("z"_hs)
            .data<&glm::vec4::w>("w"_hs)
            .data<&glm::vec4::r>("r"_hs)
            .data<&glm::vec4::g>("g"_hs)
            .data<&glm::vec4::b>("b"_hs)
            .data<&glm::vec4::a>("a"_hs);

        entt::meta_factory<glm::quat>{}
            .type("glm_quat"_hs)
            .custom<type_info>(type_info{.identifier="quat", .type_class=TYPE_CLASS_NONE})
            .data<&glm::quat::w>("w"_hs)
            .data<&glm::quat::x>("x"_hs)
            .data<&glm::quat::y>("y"_hs)
            .data<&glm::quat::z>("z"_hs);
        
    }
}