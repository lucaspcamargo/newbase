#include <newbase/physics2d/physics2d.h>
#include <sol/sol.hpp>
#include <glm/glm.hpp>

using namespace nb;

void physics2d::bind(void *state)
{
    sol::state_view lua{reinterpret_cast<lua_State*>(state)};
    lua["system_physics2d"] = this->metatype_id();
    //lua.set_function("physics2d_apply_force", [this](entt::id_type id) -> bool {
    //    return this->action_is_pressed(id);
    //});
}