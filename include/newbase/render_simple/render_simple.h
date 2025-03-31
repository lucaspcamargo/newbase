#pragma once

#include <newbase/system.h>

namespace nb {

class render_simple : public system {
public:
    render_simple();
    ~render_simple();

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(nb::step_phase) override;
    bool event(SDL_Event * ) override;

    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef cfg) override;
    entt::id_type metatype_id() override { return entt::hashed_string{"render_simple"}.value(); }

private:
    void draw_perf();

    SDL_Window *_win;
    SDL_Renderer* _render;
    float _scale;
    int _wx, _wy;
};

}
