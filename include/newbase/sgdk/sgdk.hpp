#pragma once

#include <newbase/system.hpp>

namespace nb {

struct sgdk_p;

// this system provides a C API for compatibility with SGDK games
// it needs to:
// - implement an emulated VDP
// - implement emulated audio chips (YM2612 and PSG)
// - expose input as the C API expects it
// - any other support systems
// - does not have to be fully complete, games will inform what API is needed
// - games must use the native newbase APIs when appropriate, macros are ok
class sgdk : public system
{
public:
    sgdk();
    ~sgdk();

    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef cfg) override {return 0;}
    entt::id_type metatype_id() override { return entt::hashed_string{"sgdk"}.value(); }
    //bool can_bind() override { return true; }
    //void bind(void *state) override;

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(step_phase) override;
    bool event(SDL_Event*) override;

private:
    sgdk_p *_d;
};

}