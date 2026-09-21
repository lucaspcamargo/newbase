#pragma once

#include <newbase/system.hpp>
#include <newbase/scene.hpp>
#include <newbase/layer.hpp>
#include <newbase/render/batcher2d.hpp>
#include <newbase/services/renderer_service.hpp>
#include <newbase/services/picker_service.hpp>
#include <newbase/utility/glm.hpp>
#include <memory>

namespace nb {

struct render_2d_p;
class rtexture;

class render_2d : public system, public renderer_service, public picker_service
{
public:
    render_2d();
    ~render_2d();

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(nb::step_phase) override;
    bool event(SDL_Event * ) override;

    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef cfg) override;
    entt::id_type metatype_id() override { return entt::hashed_string{"render_2d"}.value(); }

    int   window_width()  const override;
    int   window_height() const override;
    float display_scale() const override;

    // Legacy single-camera setup. Still functional as a fallback when no render
    // layers are configured.
    void cam_2d_setup(float cx, float cy, float wmax, float hmax) override;
    float cam_2d_scale();

    // picker_service interface
    // TODO move to nb::render namespace, generalize, drop service
    entt::entity pick(const render_layer &layer, float vp_x, float vp_y) override;

    // renderer_service interface
    // TODO remove this
    bool get_2d_extents(renderer_service::extents_2d &extents) override;

    // opaque texture management interface from renderer_service
    // for UI usage and simpler rendering purposes
    // we guarantee that the handle can be used as an ImGui TexID
    texture_handle create_texture(int w, int h) override;
    void update_texture(texture_handle tex, const void* pixels, int pitch) override;
    void destroy_texture(texture_handle tex) override;

private:
    // tries to ensure a texture is ready for rendering, uploading it
    // if possible. for internal use, so raw pointer is ok
    void _prepare_texture(rtexture *tex);

    // draws a scene using the given layer's masking, and the given viewprojection matrix
    // uses batcher2d and collect2d to do it
    // SDL_Renderer does not use NDC, so viewproj must map to render target pixel coordinates
    // caller is responsble for viewport clearing and clipping setup
    void _draw_scene(scene &scn, const glm::mat4x4 &viewproj, const render_layer &l);

    // draws the current contents of the geometry batcher
    // to the current render target
    // clip is intersected with the command's clip if not NONE
    void _draw_batches(render::batcher2d& batcher, render::clip_t clip = render::CLIP_NONE);

    // TODO remove this
    // all viewports should be well-defined and know their target size
    std::pair<glm::vec2, glm::vec2> _get_viewport_bounds(const render_layer &l);

    std::unique_ptr<render_2d_p> _d;
};

}
