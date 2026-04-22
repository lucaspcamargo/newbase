#pragma once

#include <newbase/system.hpp>

namespace nb {

class tilemap_system : public system
{
public:
    tilemap_system();
    ~tilemap_system();

    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef) override { return 0; }
    entt::id_type metatype_id() override { return entt::hashed_string{"tilemap_system"}.value(); }

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(step_phase phase) override;
    bool event(SDL_Event*) override { return true; }
};

} // namespace nb
