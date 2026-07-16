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
    void reset_gravity();

    // apply forces and torques
    bool body_force(entt::entity ent, glm::vec2 force, glm::vec2 world_point, bool awake = true);
    bool body_force_center(entt::entity ent, glm::vec2 force, bool awake = true);
    bool body_torque(entt::entity ent, float torque, bool awake = true);

    // warp body to a given position, keeping other dynamics unchanged
    bool body_warp(entt::entity ent, glm::vec2 position);

    // set linear velocity directly (or as initial velocity before first physics step)
    bool body_set_velocity(entt::entity ent, glm::vec2 vel);
    bool body_set_angular_velocity(entt::entity ent, float omega);

    // contact events collected during the last PHYSICS_UPDATE step
    unsigned int contact_begins_count() const;
    entt::entity contact_begin_a(unsigned int idx) const;
    entt::entity contact_begin_b(unsigned int idx) const;

    // character mover helpers
    bool character_set_velocity(entt::entity ent, glm::vec2 vel);
    glm::vec2 character_get_velocity(entt::entity ent) const;
    bool character_is_grounded(entt::entity ent) const;
    bool character_warp(entt::entity ent, glm::vec2 pos);

    // world-space raycast; returns vec4(fraction, nx, ny, 0) or vec4(-1,0,0,0) on miss
    glm::vec4 raycast(float x1, float y1, float x2, float y2, uint64_t mask) const;

    // world-space point query; returns the topmost entity with a body/shape overlapping the
    // point, or entt::null if none. Intended for script-driven picking (e.g. mouse/touch drag).
    entt::entity point_query(glm::vec2 world_point, uint64_t mask = 0xffffffffffffffffull) const;

    // script-driven dragging: creates a kinematic anchor body at world_point and a motor joint
    // pulling ent's body toward it, mimicking the official Box2D "mouse joint" sample. Returns a
    // drag handle (>=0) to pass to drag_update/drag_end, or -1 on failure.
    int  drag_begin(entt::entity ent, glm::vec2 world_point, float force_scale = 100.0f);
    bool drag_update(int drag_id, glm::vec2 world_point);
    void drag_end(int drag_id);

private:
    void _draw_tool_window(bool *close);
    void _step_characters(entt::registry &reg, float dt);

    physics2d_p *_d;
};

}
