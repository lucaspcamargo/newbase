#pragma once

#include <newbase/system.hpp>
#include <newbase/services/renderer_service.hpp>
#include <newbase/utility/glm.hpp>
#include <newbase/components/spatial.hpp>
#include <newbase/components/camera.hpp>
#include <unordered_map>

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

    // Legacy single-camera setup. Still functional as a fallback when no render
    // layers are configured.
    void cam_2d_setup(float cx, float cy, float wmax, float hmax);
    float cam_2d_scale();

    void set_clear_color(float r, float g, float b);

    // renderer_service interface
    bool get_2d_extents(renderer_service::extents_2d &extents) override;

    viewport_handle create_viewport(int x, int y, int w, int h,
                                    bool clear = true,
                                    float r = 0.f, float g = 0.f,
                                    float b = 0.f, float a = 1.f) override;
    void update_viewport(viewport_handle vp, int x, int y, int w, int h) override;
    void destroy_viewport(viewport_handle vp) override;

    viewport_handle default_viewport() const override;
    void reset_default_viewport() override;

    texture_handle create_texture(int w, int h) override;
    void update_texture(texture_handle tex, const void* pixels, int pitch) override;
    void destroy_texture(texture_handle tex) override;

private:
    struct viewport_entry {
        int x, y, w, h;
        bool clear;
        float r, g, b, a;
    };

    // Draw one sprite into the given viewproj transform, applying the layer mask check.
    void _draw_scene(entt::registry &reg, const glm::mat4x4 &viewproj,
                     uint32_t layer_mask, const viewport_entry &vp);

    void on_scene_change() override;

    SDL_Window    *_win;
    SDL_Renderer  *_render;
    float          _scale;
    int            _wx, _wy;
    float          _clear_r{0.f}, _clear_g{0.f}, _clear_b{0.f};

    // fallback camera used when no render layers are configured
    cspatial _fallback_spatial {};
    ccamera  _fallback_camera  {};

    std::unordered_map<viewport_handle, viewport_entry> _viewports;
    viewport_handle _next_vp_handle { 1 }; // 0 is VIEWPORT_INVALID

    // The default viewport covers the window's scene area.
    // It is auto-sized to the full window on init and resize unless
    // a caller has explicitly overridden it via update_viewport().
    viewport_handle _default_vp       { VIEWPORT_INVALID };
    bool            _default_vp_owned { false }; // true once set by an external caller
};

}
