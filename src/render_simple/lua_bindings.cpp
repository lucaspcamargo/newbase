#include <newbase/render_simple/render_simple.h>
#include <sol/sol.hpp>
#include <glm/glm.hpp>

using namespace nb;

void render_simple::bind(void *state)
{
    sol::state_view lua{reinterpret_cast<lua_State*>(state)};
    lua["system_render_simple"] = this->metatype_id();
    lua.set_function("render_window_width", [this]() -> int {
        return window_width();
    });
    lua.set_function("render_window_height", [this]() -> int {
        return window_height();
    });
    lua.set_function("render_cam_2d_setup", [this](float cx, float cy, float wmax, float hmax) -> void {
        cam_2d_setup(cx, cy, wmax, hmax);
    });
    lua.set_function("render_cam_2d_scale", [this]() -> float {
        return cam_2d_scale();
    });
}