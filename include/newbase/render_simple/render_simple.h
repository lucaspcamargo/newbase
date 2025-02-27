#pragma once

#include <newbase/system.h>

namespace nb {

class render_simple : public system {
public:
    render_simple();
    ~render_simple();

    bool init(int argc, char **argv) override;
    bool step(nb::step_phase) override;
    bool event(SDL_Event * ) override;

    SDL_InitFlags sdl_subsystems() override;

private:
    void draw_perf();

    SDL_Window *_win;
    SDL_Renderer* _render;
    float _scale;
};

}
