#include <newbase/services/rtti_info.hpp>
#include <newbase/services/ui_manager.hpp>
#include <newbase/services/viewport_geometry.hpp>
#include <newbase/reflection/data.hpp>
#include <entt/meta/factory.hpp>
#include <entt/locator/locator.hpp>

using entt::operator""_hs;

// viewport_geometry: wrap out-param getter as a value return
static nb::viewport_geometry::extents_2d _vp_get_2d_extents(nb::viewport_geometry &self)
{
    nb::viewport_geometry::extents_2d e{};
    self.get_2d_extents(e);
    return e;
}

namespace nb::rtti {

void _rtti_init_services()
{
    // viewport_geometry::extents_2d — register fields so Lua can read them
    entt::meta_factory<nb::viewport_geometry::extents_2d>{}
        .type("viewport_extents_2d"_hs)
        .custom<type_info>(type_info{.identifier="viewport_extents_2d", .type_class=TYPE_CLASS_NONE})
        .data<&nb::viewport_geometry::extents_2d::width>("width"_hs)
        .data<&nb::viewport_geometry::extents_2d::height>("height"_hs)
        .data<&nb::viewport_geometry::extents_2d::xspan>("xspan"_hs)
        .data<&nb::viewport_geometry::extents_2d::yspan>("yspan"_hs)
        .data<&nb::viewport_geometry::extents_2d::left>("left"_hs)
        .data<&nb::viewport_geometry::extents_2d::top>("top"_hs)
        .data<&nb::viewport_geometry::extents_2d::right>("right"_hs)
        .data<&nb::viewport_geometry::extents_2d::bottom>("bottom"_hs)
        .data<&nb::viewport_geometry::extents_2d::ui_scale>("ui_scale"_hs);

    entt::meta_factory<nb::viewport_geometry>{}
        .type("viewport_geometry"_hs)
        .custom<type_info>(type_info{
            .identifier = "viewport_geometry",
            .type_class = TYPE_CLASS_SERVICE,
            .data = {.service = {
                .getter = +[]() -> void* {
                    return entt::locator<nb::viewport_geometry*>::has_value()
                        ? static_cast<void*>(entt::locator<nb::viewport_geometry*>::value())
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
