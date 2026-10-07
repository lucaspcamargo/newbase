#pragma once

#include "newbase/render/batcher2d.hpp"
#include "newbase/render/types.hpp"
#include "newbase/res/texture.hpp"
#include <newbase/system.hpp>
#include <newbase/services/renderer_service.hpp>
#include <newbase/services/picker_service.hpp>
#include <newbase/utility/glm.hpp>
#include <vector>


struct SDL_GPURenderPass;

namespace nb {

struct render_gpu_p;
struct rshader;

class render_gpu final : public system, public renderer_service, public picker_service
{
public:
    render_gpu();
    ~render_gpu();

    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef cfg) override { return SDL_INIT_VIDEO; }
    entt::id_type metatype_id() override { return entt::hashed_string{"render_gpu"}.value(); }

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(nb::step_phase) override;
    bool event(SDL_Event*) override;
    void shutdown() override;

    int   window_width()  const override;
    int   window_height() const override;
    float display_scale() const override;

    // RTT target management interface
    // prefer to use these via render::target_ref
    render::target_id_t target_create(const render::target_desc& desc) override;
    bool target_destroy(render::target_id_t id) override;
    std::shared_ptr<rtexture> target_get_color_texture(render::target_id_t id) const override;
    std::shared_ptr<rtexture> target_get_depth_texture(render::target_id_t id) const override;
    bool           target_has_depth(render::target_id_t id) const override;
    glm::ivec2     target_get_size(render::target_id_t id) const override;


    // picker_service
    entt::entity pick(const render_layer& layer, float vp_x, float vp_y) override;

private:
    // a render loop iteration
    void _render();

    // send commands to render ui geometry on the given render paas
    void _render_ui();

    // send commands to render 2d geometry data in the batcher, on the current pass
    // proj must be a matrix that maps from 2d space to SDL_GPU clip space, col-major
    // In contrast to render_2d:
    // Not that a valid clip is MANDATORY! We need to know target size to rest the scissor
    void _render_2d_batches(render::batcher2d::sync_id_t sync_point, const glm::mat4 &proj, render::clip_t clip);

    // Where whe currently do 3D rendering in a simple, naive fashion
    // Needs rework when we introduce the 3d versions of collector and batcher
    void _render_3d_layer(const render_layer &l);

    std::unique_ptr<render_gpu_p> _d;
};

} // namespace nb
