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

    // picker_service interface
    // TODO move to nb::render namespace, generalize, drop service
    entt::entity pick(const render_layer &layer, float vp_x, float vp_y) override;

    // opaque texture management interface from renderer_service
    // for UI usage and simpler rendering purposes
    // we guarantee that the handle can be used as an ImGui TexID
    texture_handle create_texture(int w, int h) override;
    void update_texture(texture_handle tex, const void* pixels, int pitch) override;
    void destroy_texture(texture_handle tex) override;

    // RTT target management interface
    // prefer to use these via render::target_ref
    render::target_id_t target_create(const render::target_desc& desc) override;
    void target_destroy(render::target_id_t id) override;
    std::shared_ptr<rtexture> target_get_color_texture(render::target_id_t id) const override;
    std::shared_ptr<rtexture> target_get_depth_texture(render::target_id_t id) const override;
    glm::ivec2     target_get_size(render::target_id_t id) const override;
    bool           target_has_depth(render::target_id_t id) const override;

private:
    // tries to ensure a texture is ready for rendering, uploading it
    // if possible. for internal use, so raw pointer is fine
    void _prepare_texture(rtexture *tex);

    // draws a scene using the given layer's masking, and the given VP matrix
    // uses batcher2d and collect2d to do it
    // SDL_Renderer does not use NDC, so viewproj must map to target pixel coords
    // will draw batches with viewport as the main clip
    void _draw_scene(scene &scn, const glm::mat4x4 &viewproj, const render_layer &l);

    // draws the current contents of the geometry batcher
    // to the current render target
    // clip is intersected with the command's clip if not NONE
    void _draw_batches(render::batcher2d& batcher, render::clip_t clip = render::CLIP_NONE);

    std::unique_ptr<render_2d_p> _d;
};

}
