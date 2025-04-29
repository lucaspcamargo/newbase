#pragma once

#include <newbase/system.h>
#include <newbase/services/viewport_geometry.h>

namespace nb {

class render_simple : public system, public viewport_geometry
{
public:
    render_simple();
    ~render_simple();

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(nb::step_phase) override;
    bool event(SDL_Event * ) override;

    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef cfg) override;
    entt::id_type metatype_id() override { return entt::hashed_string{"render_simple"}.value(); }
    bool can_bind() override { return true; }
    void bind(void *lua) override;

    int window_width();
    int window_height();

    void cam_2d_setup(float cx, float cy, float wmax, float hmax);
    float cam_2d_scale();

    // service: viewport_geometry
    bool get_2d_extents(viewport_geometry::extents_2d &extents) override;

private:
    void draw_perf();

    SDL_Window *_win;
    SDL_Renderer* _render;
    float _scale;
    int _wx, _wy;
};

}
