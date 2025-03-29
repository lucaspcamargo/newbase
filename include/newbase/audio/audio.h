#pragma once

#include "newbase/system.h"
#include <entt/entt.hpp>

namespace nb {

class audio : public system {
public:
    audio();
    ~audio();

    SDL_InitFlags sdl_subsystems() override;

    bool init(int argc, char ** argv) override;
    bool step(nb::step_phase ) override;
    bool event(SDL_Event * ) override;

    // we want to have a simple API for music and sfx,
    // that is easy to invoke via reflection
    // no need for fancy features, this should be good enough for simple games 

    // background music
    bool bgm_play(entt::id_type res_id);
    bool bgm_playing();
    bool bgm_stop();
    void bgm_gain(float gain);

    // sound effects 
    bool sfx_play(entt::id_type res_id, float gain);

};

}

