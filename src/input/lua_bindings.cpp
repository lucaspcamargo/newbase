#include <newbase/input/input.h>
#include <sol/sol.hpp>
#include <glm/glm.hpp>

using namespace nb;

void input::bind(void *state)
{
    sol::state_view lua{reinterpret_cast<lua_State*>(state)};
    lua["system_input"] = this->metatype_id();
    lua.set_function("input_action_is_pressed", [this](entt::id_type id) -> bool {
        return this->action_is_pressed(id);
    });
    lua.set_function("input_action_was_pressed", [this](entt::id_type id) -> bool {
        return this->action_was_pressed(id);
    });
    lua.set_function("input_action_was_released", [this](entt::id_type id) -> bool {
        return this->action_was_released(id);
    });
    lua.set_function("input_action_direction", [this](entt::id_type id) -> glm::vec3 {
        auto dir = this->action_direction(id);
        return glm::vec3{dir[0], dir[1], dir[2]};
    });
}