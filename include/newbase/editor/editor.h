#pragma once

#include <newbase/system.h>

namespace nb {
    
class editor final : public nb::system
{
public:
    editor() {}
    ~editor() {}


    SDL_InitFlags sdl_subsystems() override {return 0;}
    bool init(int argc, char **argv) override;
    bool step(step_phase) override;
    bool event(SDL_Event*) override;

};

}