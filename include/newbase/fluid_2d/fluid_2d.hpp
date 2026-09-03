#pragma once

#include <newbase/system.hpp>

namespace nb
{

struct fluid_2d_p;

class fluid_2d : public system
{
public:
    fluid_2d();
    ~fluid_2d();

    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef) override { return 0; }
    entt::id_type metatype_id() override { return entt::hashed_string{"fluid_2d"}.value(); }

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(step_phase phase) override;
    bool event(SDL_Event *event) override;

private:
    fluid_2d_p *_d;
};

}