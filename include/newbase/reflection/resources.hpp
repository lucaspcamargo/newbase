#pragma once

#include <newbase/reflection/data.hpp>
#include <newbase/res/resource.hpp>
#include <newbase/log.hpp>
#include <memory>

// Utility for registering and handling shared_ptr<resource_t>

namespace nb::rtti
{



    template<typename T>
    auto res_ptr_registration(const char *name_str)
    {
        std::string name_ptr {name_str};
        name_ptr += "_ptr";

        return entt::meta_factory<std::shared_ptr<T>>{}
        .type(entt::hashed_string{name_ptr.c_str()}.value())
        .template ctor<>()
        .template custom<type_info>(type_info{
            .identifier = name_ptr.c_str(),
            .type_class = TYPE_CLASS_RESOURCE_PTR,
            .data = {.resource_ptr = {
                .resource_type_id = entt::hashed_string{name_str}.value(),
                .get_ptr = +[](const entt::meta_any& a) -> std::shared_ptr<nb::resource> {
                    auto* p = a.try_cast<std::shared_ptr<T>>();
                    return p ? *p : nullptr;
                },
                .set_ptr = +[](entt::meta_any& a, std::shared_ptr<nb::resource> p) {
                    if(!a.assign(std::static_pointer_cast<T>(p))) {
                        auto target_type = a.type().info().name(); /*not sure if this is correct btw*/
                        auto source_type = typeid(T).name();
                        log::warn("[res] resource pointer assignment failed: target='%s' source='%s'",
                        target_type.length() ? std::string{target_type}.c_str() : "<unknown>",
                        source_type ? source_type : "<unknown>");
                    }
                }
            }}
        });
    }
}
