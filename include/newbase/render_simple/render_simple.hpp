#pragma once

#include <newbase/system.hpp>
#include <newbase/services/renderer_service.hpp>

namespace nb {

class render_simple : public system, public renderer_service
{
public:
    render_simple();
    ~render_simple();

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(nb::step_phase) override;
    bool event(SDL_Event * ) override;

    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef cfg) override;
    entt::id_type metatype_id() override { return entt::hashed_string{"render_simple"}.value(); }

    int window_width();
    int window_height();

    void cam_2d_setup(float cx, float cy, float wmax, float hmax);
    float cam_2d_scale();

    // service: renderer_service
    bool get_2d_extents(renderer_service::extents_2d &extents) override;
    texture_handle create_texture(int w, int h) override;
    void update_texture(texture_handle tex, const void* pixels, int pitch) override;
    void destroy_texture(texture_handle tex) override;

private:

    SDL_Window *_win;
    SDL_Renderer* _render;
    float _scale;
    int _wx, _wy;
};

}
