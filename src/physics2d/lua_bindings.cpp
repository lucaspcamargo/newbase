#include <newbase/physics2d/physics2d.hpp>
#include <sol/sol.hpp>
#include <glm/glm.hpp>

using namespace nb;

void physics2d::bind(void *state)
{
    sol::state_view lua{reinterpret_cast<lua_State*>(state)};
    lua["system_physics2d"] = this->metatype_id();
    lua.set_function("physics2d_set_gravity", [this](glm::vec2 grav) {
        this->set_gravity(grav);
    });
    lua.set_function("physics2d_body_force", [this](entt::entity id, glm::vec2 force, glm::vec2 world_point) -> bool {
        return this->body_force(id, force, world_point);
    });
    lua.set_function("physics2d_body_force_center", [this](entt::entity id, glm::vec2 force) -> bool {
        return this->body_force_center(id, force);
    });
    lua.set_function("physics2d_body_torque", [this](entt::entity id, float torque) -> bool {
        return this->body_torque(id, torque);
    });
    lua.set_function("physics2d_body_warp", [this](entt::entity id, glm::vec2 pos) -> bool {
        return this->body_warp(id, pos);
    });
}