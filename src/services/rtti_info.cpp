#include <newbase/services/rtti_info.hpp>
#include <newbase/services/ui_manager.hpp>
#include <newbase/services/renderer_service.hpp>
#include <newbase/reflection/data.hpp>
#include <entt/meta/factory.hpp>
#include <entt/locator/locator.hpp>

using entt::operator""_hs;

// renderer_service: wrap out-param getter as a value return
static nb::renderer_service::extents_2d _vp_get_2d_extents(nb::renderer_service &self)
{
    nb::renderer_service::extents_2d e{};
    self.get_2d_extents(e);
    return e;
}

namespace nb::rtti {

void _rtti_init_services()
{
    // renderer_service::extents_2d — register fields so Lua can read them
    entt::meta_factory<nb::renderer_service::extents_2d>{}
        .type("viewport_extents_2d"_hs)
        .custom<type_info>(type_info{.identifier="viewport_extents_2d", .type_class=TYPE_CLASS_NONE})
        .data<&nb::renderer_service::extents_2d::width>("width"_hs)
        .data<&nb::renderer_service::extents_2d::height>("height"_hs)
        .data<&nb::renderer_service::extents_2d::xspan>("xspan"_hs)
        .data<&nb::renderer_service::extents_2d::yspan>("yspan"_hs)
        .data<&nb::renderer_service::extents_2d::left>("left"_hs)
        .data<&nb::renderer_service::extents_2d::top>("top"_hs)
        .data<&nb::renderer_service::extents_2d::right>("right"_hs)
        .data<&nb::renderer_service::extents_2d::bottom>("bottom"_hs)
        .data<&nb::renderer_service::extents_2d::ui_scale>("ui_scale"_hs);

    entt::meta_factory<nb::renderer_service>{}
        .type("renderer_service"_hs)
        .custom<type_info>(type_info{
            .identifier = "renderer_service",
            .type_class = TYPE_CLASS_SERVICE,
            .data = {.service = {
                .getter = +[]() -> void* {
                    return entt::locator<nb::renderer_service*>::has_value()
                        ? static_cast<void*>(entt::locator<nb::renderer_service*>::value())
                        : nullptr;
                }
            }}
        })
        .func<&_vp_get_2d_extents>("get_2d_extents"_hs)
            .custom<func_info>(func_info{"get_2d_extents"});

    entt::meta_factory<nb::ui_manager>{}
        .type("ui_manager"_hs)
        .custom<type_info>(type_info{
            .identifier = "ui_manager",
            .type_class = TYPE_CLASS_SERVICE,
            .data = {.service = {
                .getter = +[]() -> void* {
                    return entt::locator<nb::ui_manager*>::has_value()
                        ? static_cast<void*>(entt::locator<nb::ui_manager*>::value())
                        : nullptr;
                }
            }}
        })
        .func<&nb::ui_manager::toggle_tool_window>("toggle_tool_window"_hs)
            .custom<func_info>(func_info{"toggle_tool_window"})
        .func<&nb::ui_manager::unregister_tool_window>("unregister_tool_window"_hs)
            .custom<func_info>(func_info{"unregister_tool_window"});
}

} // namespace nb::rtti
