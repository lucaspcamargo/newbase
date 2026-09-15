#pragma once

#include <newbase/system.hpp>

namespace nb {

class particle_system : public system
{
public:
    particle_system();
    ~particle_system();

    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef cfg) override { return 0; }
    entt::id_type metatype_id() override { return entt::hashed_string{"particle_system"}.value(); }

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(step_phase phase) override;
    bool event(SDL_Event*) override { return true; }

    void burst(entt::entity ent, int count);
};

} // namespace nb
