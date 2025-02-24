#pragma once

#include "newbase/system.h"

namespace nb {

class audio : public system {
public:
    audio();
    ~audio();

    SDL_InitFlags sdl_subsystems() override;

    bool init(int argc, char ** argv) override;
    bool step(nb::step_phase ) override;
    bool event(SDL_Event * ) override;
};

}

