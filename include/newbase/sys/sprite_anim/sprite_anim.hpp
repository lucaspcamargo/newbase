#pragma once

#include <newbase/system.hpp>

namespace nb {

class sprite_anim : public system
{
public:
    sprite_anim();
    ~sprite_anim();

    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef) override { return 0; }
    entt::id_type metatype_id() override { return entt::hashed_string{"sprite_anim"}.value(); }

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(step_phase phase) override;
    bool event(SDL_Event*) override { return true; }
};

} // namespace nb
