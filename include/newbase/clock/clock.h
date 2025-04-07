#pragma once

#include <newbase/system.h>

namespace nb {

// this system takes care of "update callbacks" and other timing stuff
class clock : public system
{
public:
    clock();
    ~clock();

    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef cfg) override {return 0;}
    entt::id_type metatype_id() override { return entt::hashed_string{"clock"}.value(); }
    bool can_bind() override { return false; }
    void bind(void *state) override {};

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(step_phase) override;
    bool event(SDL_Event*) override;


private:

};

}