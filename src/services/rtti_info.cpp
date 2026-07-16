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
static nb::viewport_handle _vp_default_viewport(nb::renderer_service &self) { return self.default_viewport(); }
static int   _vp_window_width (const nb::renderer_service &self) { return self.window_width(); }
static int   _vp_window_height(const nb::renderer_service &self) { return self.window_height(); }
static float _vp_display_scale(const nb::renderer_service &self) { return self.display_scale(); }
static void  _vp_cam_2d_setup(nb::renderer_service &self, float cx, float cy, float wmax, float hmax) { self.cam_2d_setup(cx, cy, wmax, hmax); }
static void  _vp_set_clear_color(nb::renderer_service &self, float r, float g, float b) { self.set_clear_color(r, g, b); }

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
        .data<&nb::renderer_service::extents_2d::ui_scale>("ui_scale"_hs)
        .data<&nb::renderer_service::extents_2d::screen_x>("screen_x"_hs)
        .data<&nb::renderer_service::extents_2d::screen_y>("screen_y"_hs);

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
            .custom<func_info>(func_info{"get_2d_extents"})
        .func<&_vp_default_viewport>("default_viewport"_hs)
            .custom<func_info>(func_info{"default_viewport"})
        .func<&_vp_window_width>("window_width"_hs)
            .custom<func_info>(func_info{"window_width"})
        .func<&_vp_window_height>("window_height"_hs)
            .custom<func_info>(func_info{"window_height"})
        .func<&_vp_display_scale>("display_scale"_hs)
            .custom<func_info>(func_info{"display_scale"})
        .func<&_vp_cam_2d_setup>("cam_2d_setup"_hs)
            .custom<func_info>(func_info{"cam_2d_setup"})
        .func<&_vp_set_clear_color>("set_clear_color"_hs)
            .custom<func_info>(func_info{"set_clear_color"});

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
