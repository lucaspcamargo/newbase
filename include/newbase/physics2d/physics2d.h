#pragma once

#include <newbase/system.h>
#include <glm/vec2.hpp>

namespace nb
{

struct physics2d_p;

// this system takes care of "update callbacks" and other timing stuff
class physics2d : public system
{
public:
    physics2d();
    ~physics2d();

    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef cfg) override {return 0;}
    entt::id_type metatype_id() override { return entt::hashed_string{"physics2d"}.value(); }
    bool can_bind() override { return true; }
    void bind(void *state) override;

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(step_phase) override;
    bool event(SDL_Event*) override;

    bool body_force(entt::entity ent, glm::vec2 force, glm::vec2 world_point, bool awake = true);
    bool body_force_center(entt::entity ent, glm::vec2 force, bool awake = true);
    bool body_torque(entt::entity ent, float torque, bool awake = true);

private:
    physics2d_p *_d;
};

}