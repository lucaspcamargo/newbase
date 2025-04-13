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
}