#pragma once
#include <newbase/system.hpp>

class demo_system : public nb::system
{
public:
    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef) override { return 0; }
    entt::id_type metatype_id() override { return entt::hashed_string{"demo"}.value(); }

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(nb::step_phase) override { return true; }
    bool event(SDL_Event*) override { return true; }
};
