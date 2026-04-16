#pragma once

#include <newbase/system.hpp>
#include <newbase/utility/glm.hpp>

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

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(step_phase) override;
    bool event(SDL_Event*) override;

    void set_gravity(glm::vec2 grav);

    // apply forces and torques
    bool body_force(entt::entity ent, glm::vec2 force, glm::vec2 world_point, bool awake = true);
    bool body_force_center(entt::entity ent, glm::vec2 force, bool awake = true);
    bool body_torque(entt::entity ent, float torque, bool awake = true);

    // warp body to a given position, keeping other dynamics unchanged
    bool body_warp(entt::entity ent, glm::vec2 position);

    // set linear velocity directly (or as initial velocity before first physics step)
    bool body_set_velocity(entt::entity ent, glm::vec2 vel);

    // contact events collected during the last PHYSICS_UPDATE step
    unsigned int contact_begins_count() const;
    entt::entity contact_begin_a(unsigned int idx) const;
    entt::entity contact_begin_b(unsigned int idx) const;

private:
    void _draw_tool_window(bool *close);

    physics2d_p *_d;
};

}