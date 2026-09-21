#include <newbase/services/rtti_info.hpp>
#include <newbase/services/ui_manager.hpp>
#include <newbase/services/renderer_service.hpp>
#include <newbase/reflection/data.hpp>
#include <entt/meta/factory.hpp>
#include <entt/locator/locator.hpp>

using entt::operator""_hs;

static int   _vp_window_width (const nb::renderer_service &self) { return self.window_width(); }
static int   _vp_window_height(const nb::renderer_service &self) { return self.window_height(); }
static float _vp_display_scale(const nb::renderer_service &self) { return self.display_scale(); }

namespace nb::rtti {

void _rtti_init_services()
{
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
        .func<&_vp_window_width>("window_width"_hs)
            .custom<func_info>(func_info{"window_width"})
        .func<&_vp_window_height>("window_height"_hs)
            .custom<func_info>(func_info{"window_height"})
        .func<&_vp_display_scale>("display_scale"_hs)
            .custom<func_info>(func_info{"display_scale"});

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
        .custom<func_info>(func_info{"unregister_tool_window"})
        .func<&nb::ui_manager::central_viewport>("central_viewport"_hs)
        .custom<func_info>(func_info{"central_viewport"});
}

} // namespace nb::rtti
