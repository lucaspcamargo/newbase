#pragma once

#include <newbase/system.hpp>
#include <newbase/services/renderer_service.hpp>
#include <newbase/services/picker_service.hpp>
#include <newbase/utility/glm.hpp>
#include <newbase/components/spatial.hpp>
#include <newbase/components/camera.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <unordered_map>
#include <vector>
#include <SDL3_shadercross/SDL_shadercross.h>

namespace nb {

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

    int   window_width()  const override { return _wx; }
    int   window_height() const override { return _wy; }
    float display_scale() const override { return _scale; }
    void  cam_2d_setup(float cx, float cy, float wmax, float hmax) override;
    void  set_clear_color(float r, float g, float b) override;

    // picker_service
    entt::entity pick(const render_layer& layer, float vp_x, float vp_y) override;

    // renderer_service
    bool get_2d_extents(renderer_service::extents_2d& extents) override;

    viewport_handle default_viewport() const override { return _default_vp; }
    void reset_default_viewport() override;
    viewport_handle create_viewport(int x, int y, int w, int h,
                                    bool clear = true,
                                    float r = 0.f, float g = 0.f,
                                    float b = 0.f, float a = 1.f) override;
    void update_viewport(viewport_handle vp, int x, int y, int w, int h) override;
    void destroy_viewport(viewport_handle vp) override;

    texture_handle create_texture(int w, int h) override;
    void update_texture(texture_handle tex, const void* pixels, int pitch) override;
    void destroy_texture(texture_handle tex) override;

private:
    // Both vertex formats are 32 bytes, matching geometry_buffer_2d::vertex layout
    struct sprite_vertex { float x, y, u, v, r, g, b, a; };
    struct mesh_vertex   { float x, y, u, v, r, g, b, a; };

    struct viewport_entry {
        int x, y, w, h;
        bool clear;
        float r, g, b, a;
    };

    struct scene_tex_entry {
        SDL_GPUTexture* tex   { nullptr };
        bool            ready { false };
        int             w     { 0 };
        int             h     { 0 };
    };

    struct service_tex_entry {
        SDL_GPUTexture*        tex          { nullptr };
        SDL_GPUTransferBuffer* transfer_buf { nullptr };
        int w, h;
    };

    struct draw_call {
        enum { SPRITE, MESH, MESH_TEX } kind;
        SDL_GPUTexture* tex;
        glm::mat4       viewproj;
        uint32_t        vert_offset;
        uint32_t        vert_count;
        uint32_t        idx_offset;
        uint32_t        idx_count;
    };

    SDL_GPUShader* _compile_shader(const uint32_t* spirv, size_t spirv_size,
                                   SDL_ShaderCross_ShaderStage stage,
                                   const SDL_ShaderCross_GraphicsShaderResourceInfo& res_info);
    bool _init_pipelines();
    void _upload_scene_tex(SDL_GPUCommandBuffer* cmd, SDL_GPUCopyPass* cp,
                           struct rtexture* rtex, scene_tex_entry& entry);
    void _draw_scene(entt::registry& reg, const glm::mat4& viewproj,
                     uint32_t layer_mask, const viewport_entry& vp,
                     std::vector<sprite_vertex>& sv, std::vector<mesh_vertex>& mv,
                     std::vector<uint32_t>& mi, std::vector<draw_call>& dcs);
    void on_scene_change() override;

    SDL_Window*     _win    { nullptr };
    SDL_GPUDevice*  _device { nullptr };
    float           _scale  { 1.f };
    int             _wx     { 0 }, _wy { 0 };

    SDL_GPUGraphicsPipeline* _pipeline_sprite  { nullptr };
    SDL_GPUGraphicsPipeline* _pipeline_mesh2d  { nullptr };
    SDL_GPUSampler*          _default_sampler  { nullptr };

    SDL_GPUBuffer*         _vbuf_sprite  { nullptr };
    SDL_GPUBuffer*         _vbuf_mesh    { nullptr };
    SDL_GPUBuffer*         _ibuf_mesh    { nullptr };
    SDL_GPUTransferBuffer* _tbuf_sprite  { nullptr };
    SDL_GPUTransferBuffer* _tbuf_mesh    { nullptr };
    SDL_GPUTransferBuffer* _titbuf_mesh  { nullptr };
    static constexpr int   MAX_VERTS     = 65536;

    std::unordered_map<struct rtexture*, scene_tex_entry>  _tex_cache;
    std::unordered_map<void*, service_tex_entry>           _service_textures;

    std::unordered_map<viewport_handle, viewport_entry> _viewports;
    viewport_handle _next_vp_handle    { 1 };
    viewport_handle _default_vp        { VIEWPORT_INVALID };
    bool            _default_vp_owned  { false };

    cspatial _fallback_spatial {};
    ccamera  _fallback_camera  {};

    bool _has_ui { false };
};

} // namespace nb
