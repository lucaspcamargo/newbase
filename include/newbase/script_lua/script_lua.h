#pragma once

#include <newbase/system.h>

namespace nb {

class script_lua final : public system {
public:
    script_lua() = default;
    ~script_lua() {}

    SDL_InitFlags sdl_subsystems() override;

    bool init(int argc, char **argv) override;
    bool step(step_phase) override;
    bool event(SDL_Event*) override;
};

}
