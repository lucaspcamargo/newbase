#include <newbase/render_gpu/render_gpu.hpp>
#include <newbase/engine.hpp>
#include <newbase/scene.hpp>
#include <newbase/layer.hpp>
#include <newbase/components/sprite.hpp>
#include <newbase/components/mesh2d.hpp>
#include <newbase/components/particle_emitter.hpp>
#include <newbase/components/spatial.hpp>
#include <newbase/components/structure.hpp>
#include <newbase/components/camera.hpp>
#include <newbase/components/layers.hpp>
#include <newbase/res/sprite.hpp>
#include <newbase/res/texture.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/reflection/contexts.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/ui/imgui_style.hpp>
#include <newbase/services/ui_manager.hpp>
#include <newbase/log.hpp>

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlgpu3.h"
#include <SDL3_shadercross/SDL_shadercross.h>
#include <entt/entt.hpp>
#include <newbase/utility/glm.hpp>
#include <ryml.hpp>
#include <ryml_std.hpp>
#include <tracy/Tracy.hpp>

#include <cmath>
#include <cstring>
#include <vector>

#include "render_gpu_shaders.hpp"

using namespace nb;
using entt::operator""_hs;

// ---- vertex layout (matches geometry_buffer_2d::vertex and sprite_vertex) --------------------
// location 0: vec2 pos  (offset  0, 8 bytes)
// location 1: vec2 uv   (offset  8, 8 bytes)
// location 2: vec4 color(offset 16, 16 bytes)
// stride = 32 bytes

// ---- helpers -----------------------------------------------------------------------------------

static SDL_Rect _safe {};

static SDL_GPUBuffer* _create_gpu_buffer(SDL_GPUDevice* dev, SDL_GPUBufferUsageFlags usage, uint32_t size)
{
    SDL_GPUBufferCreateInfo info {};
    info.usage = usage;
    info.size  = size;
    return SDL_CreateGPUBuffer(dev, &info);
}

static SDL_GPUTransferBuffer* _create_transfer_buffer(SDL_GPUDevice* dev, SDL_GPUTransferBufferUsage usage, uint32_t size)
{
    SDL_GPUTransferBufferCreateInfo info {};
    info.usage = usage;
    info.size  = size;
    return SDL_CreateGPUTransferBuffer(dev, &info);
}

// ---- render_gpu --------------------------------------------------------------------------------

render_gpu::render_gpu()
{
    entt::locator<renderer_service*>::emplace(this);
    entt::locator<picker_service*>::emplace(this);
}
render_gpu::~render_gpu()
{
    if (_has_ui)
    {
        ImGui_ImplSDLGPU3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        if (auto *ui_mgr = entt::locator<ui_manager*>::value())
            ui_mgr->ui_destroy();
    }

    if (_device)
    {
        SDL_WaitForGPUIdle(_device);

        for (auto &[k, e] : _tex_cache)
            if (e.tex) SDL_ReleaseGPUTexture(_device, e.tex);

        for (auto &[k, e] : _service_textures)
        {
            if (e.tex)          SDL_ReleaseGPUTexture(_device, e.tex);
            if (e.transfer_buf) SDL_ReleaseGPUTransferBuffer(_device, e.transfer_buf);
        }

        if (_vbuf_sprite) SDL_ReleaseGPUBuffer(_device, _vbuf_sprite);
        if (_vbuf_mesh)   SDL_ReleaseGPUBuffer(_device, _vbuf_mesh);
        if (_ibuf_mesh)   SDL_ReleaseGPUBuffer(_device, _ibuf_mesh);
        if (_tbuf_sprite) SDL_ReleaseGPUTransferBuffer(_device, _tbuf_sprite);
        if (_tbuf_mesh)   SDL_ReleaseGPUTransferBuffer(_device, _tbuf_mesh);
        if (_titbuf_mesh) SDL_ReleaseGPUTransferBuffer(_device, _titbuf_mesh);

        if (_pipeline_sprite) SDL_ReleaseGPUGraphicsPipeline(_device, _pipeline_sprite);
        if (_pipeline_mesh2d) SDL_ReleaseGPUGraphicsPipeline(_device, _pipeline_mesh2d);
        if (_default_sampler) SDL_ReleaseGPUSampler(_device, _default_sampler);

        if (_win) SDL_ReleaseWindowFromGPUDevice(_device, _win);
        SDL_DestroyGPUDevice(_device);
    }

    if (_win) SDL_DestroyWindow(_win);
    SDL_ShaderCross_Quit();
}

bool render_gpu::init(ryml::ConstNodeRef cfg)
{
    log::info("[render_gpu] init");

    // -- window ---------------------------------------------------------------------------------
    const Uint32 window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_MAXIMIZED;
    _win = SDL_CreateWindow(SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING),
                            1024, 768, window_flags);
    if (!_win)
    {
        log::error("[render_gpu] SDL_CreateWindow: %s", SDL_GetError());
        return false;
    }
    _scale = SDL_GetWindowDisplayScale(_win);
    SDL_SetWindowPosition(_win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(_win);
    SDL_GetWindowSizeInPixels(_win, &_wx, &_wy);
    log::info("[render_gpu] window %dx%d scale=%.2f", _wx, _wy, _scale);

    // -- GPU device -----------------------------------------------------------------------------
    if (!SDL_ShaderCross_Init())
        log::warn("[render_gpu] SDL_ShaderCross_Init failed: %s", SDL_GetError());

    _device = SDL_CreateGPUDevice(SDL_ShaderCross_GetSPIRVShaderFormats(), false, nullptr);
    if (!_device)
    {
        log::error("[render_gpu] SDL_CreateGPUDevice: %s", SDL_GetError());
        return false;
    }
    log::info("[render_gpu] GPU driver: %s", SDL_GetGPUDeviceDriver(_device));

    if (!SDL_ClaimWindowForGPUDevice(_device, _win))
    {
        log::error("[render_gpu] SDL_ClaimWindowForGPUDevice: %s", SDL_GetError());
        return false;
    }

    // -- window icon ----------------------------------------------------------------------------
    auto icon_tex = rman().get<rtexture>("_nb_core/icon_192.png"_hs);
    if (icon_tex && icon_tex->surf)
        SDL_SetWindowIcon(_win, icon_tex->surf);

    // -- ImGui ----------------------------------------------------------------------------------
    ui_manager* ui_mgr = entt::locator<ui_manager*>::value();
    if (ui_mgr)
        _has_ui = ui_mgr->ui_init();
    else
        log::warn("[render_gpu] no ui_manager service");

    if (_has_ui)
    {
        ImGui_ImplSDL3_InitForOther(_win);
        ImGui_ImplSDLGPU3_InitInfo gpu_init {};
        gpu_init.Device            = _device;
        gpu_init.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(_device, _win);
        gpu_init.MSAASamples       = SDL_GPU_SAMPLECOUNT_1;
        ImGui_ImplSDLGPU3_Init(&gpu_init);
        ui_mgr->ui_init_finish(_scale);
    }

    // -- default sampler ------------------------------------------------------------------------
    SDL_GPUSamplerCreateInfo samp_info {};
    samp_info.min_filter    = SDL_GPU_FILTER_NEAREST;
    samp_info.mag_filter    = SDL_GPU_FILTER_NEAREST;
    samp_info.mipmap_mode   = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    samp_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samp_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    _default_sampler = SDL_CreateGPUSampler(_device, &samp_info);

    // -- pipelines ------------------------------------------------------------------------------
    if (!_init_pipelines())
        return false;

    // -- vertex / transfer buffers --------------------------------------------------------------
    const uint32_t sprite_buf_size = MAX_VERTS * sizeof(sprite_vertex);
    const uint32_t mesh_buf_size   = MAX_VERTS * sizeof(mesh_vertex);
    const uint32_t idx_buf_size    = MAX_VERTS * sizeof(uint32_t);

    _vbuf_sprite  = _create_gpu_buffer(_device, SDL_GPU_BUFFERUSAGE_VERTEX, sprite_buf_size);
    _tbuf_sprite  = _create_transfer_buffer(_device, SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, sprite_buf_size);
    _vbuf_mesh    = _create_gpu_buffer(_device, SDL_GPU_BUFFERUSAGE_VERTEX, mesh_buf_size);
    _ibuf_mesh    = _create_gpu_buffer(_device, SDL_GPU_BUFFERUSAGE_INDEX,  idx_buf_size);
    _tbuf_mesh    = _create_transfer_buffer(_device, SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, mesh_buf_size);
    _titbuf_mesh  = _create_transfer_buffer(_device, SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, idx_buf_size);

    // -- safe area / default viewport -----------------------------------------------------------
    SDL_GetWindowSafeArea(_win, &_safe);
    _default_vp = create_viewport(0, 0, _wx, _wy, false);
    log::info("[render_gpu] default viewport: %u", _default_vp);

    return true;
}

// ---- shader / pipeline helpers ----------------------------------------------------------------

SDL_GPUShader* render_gpu::_compile_shader(const uint32_t* spirv, size_t spirv_size,
                                            SDL_ShaderCross_ShaderStage stage,
                                            const SDL_ShaderCross_GraphicsShaderResourceInfo& res_info)
{
    SDL_ShaderCross_SPIRV_Info info {};
    info.bytecode      = reinterpret_cast<const Uint8*>(spirv);
    info.bytecode_size = spirv_size;
    info.entrypoint    = "main";
    info.shader_stage  = stage;
    info.props         = 0;

    SDL_GPUShader* shader = SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(_device, &info, &res_info, 0);
    if (!shader)
        log::error("[render_gpu] shader compile failed: %s", SDL_GetError());
    return shader;
}

bool render_gpu::_init_pipelines()
{
    // shared vertex layout: pos(f2), uv(f2), color(f4) — stride 32
    SDL_GPUVertexBufferDescription vbd {};
    vbd.slot              = 0;
    vbd.pitch             = sizeof(sprite_vertex);
    vbd.input_rate        = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vbd.instance_step_rate = 0;

    SDL_GPUVertexAttribute attrs[3] {};
    attrs[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,  0  };
    attrs[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,  8  };
    attrs[2] = { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 16  };

    SDL_GPUColorTargetDescription color_target {};
    color_target.format = SDL_GetGPUSwapchainTextureFormat(_device, _win);
    // standard alpha blend
    color_target.blend_state.enable_blend        = true;
    color_target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    color_target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color_target.blend_state.color_blend_op       = SDL_GPU_BLENDOP_ADD;
    color_target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    color_target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color_target.blend_state.alpha_blend_op       = SDL_GPU_BLENDOP_ADD;

    // -- sprite pipeline (textured) -------------------------------------------------------------
    SDL_ShaderCross_GraphicsShaderResourceInfo sprite_vert_res {};
    sprite_vert_res.num_uniform_buffers = 1;
    sprite_vert_res.num_samplers        = 0;
    sprite_vert_res.num_storage_buffers = 0;
    sprite_vert_res.num_storage_textures = 0;

    SDL_ShaderCross_GraphicsShaderResourceInfo sprite_frag_res {};
    sprite_frag_res.num_uniform_buffers = 0;
    sprite_frag_res.num_samplers        = 1;
    sprite_frag_res.num_storage_buffers = 0;
    sprite_frag_res.num_storage_textures = 0;

    SDL_GPUShader* sv = _compile_shader(k_sprite_vert_spv, k_sprite_vert_spv_size,
                                        SDL_SHADERCROSS_SHADERSTAGE_VERTEX,   sprite_vert_res);
    SDL_GPUShader* sf = _compile_shader(k_sprite_frag_spv, k_sprite_frag_spv_size,
                                        SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, sprite_frag_res);
    if (!sv || !sf) return false;

    SDL_GPUGraphicsPipelineCreateInfo pipe {};
    pipe.vertex_shader   = sv;
    pipe.fragment_shader = sf;
    pipe.vertex_input_state.vertex_buffer_descriptions = &vbd;
    pipe.vertex_input_state.num_vertex_buffers         = 1;
    pipe.vertex_input_state.vertex_attributes          = attrs;
    pipe.vertex_input_state.num_vertex_attributes      = 3;
    pipe.primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipe.target_info.color_target_descriptions         = &color_target;
    pipe.target_info.num_color_targets                 = 1;

    _pipeline_sprite = SDL_CreateGPUGraphicsPipeline(_device, &pipe);
    SDL_ReleaseGPUShader(_device, sv);
    SDL_ReleaseGPUShader(_device, sf);
    if (!_pipeline_sprite) { log::error("[render_gpu] sprite pipeline: %s", SDL_GetError()); return false; }

    // -- mesh2d pipeline (vertex-colored) -------------------------------------------------------
    SDL_ShaderCross_GraphicsShaderResourceInfo mesh_vert_res {};
    mesh_vert_res.num_uniform_buffers = 1;

    SDL_ShaderCross_GraphicsShaderResourceInfo mesh_frag_res {};
    // no samplers

    SDL_GPUShader* mv = _compile_shader(k_mesh2d_vert_spv, k_mesh2d_vert_spv_size,
                                        SDL_SHADERCROSS_SHADERSTAGE_VERTEX,   mesh_vert_res);
    SDL_GPUShader* mf = _compile_shader(k_mesh2d_frag_spv, k_mesh2d_frag_spv_size,
                                        SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, mesh_frag_res);
    if (!mv || !mf) return false;

    pipe.vertex_shader   = mv;
    pipe.fragment_shader = mf;
    _pipeline_mesh2d = SDL_CreateGPUGraphicsPipeline(_device, &pipe);
    SDL_ReleaseGPUShader(_device, mv);
    SDL_ReleaseGPUShader(_device, mf);
    if (!_pipeline_mesh2d) { log::error("[render_gpu] mesh2d pipeline: %s", SDL_GetError()); return false; }

    return true;
}

// ---- step -------------------------------------------------------------------------------------

bool render_gpu::step(nb::step_phase phase)
{
    if (phase == step_phase::PRE_UPDATE)
    {
        ZoneScopedN("RenderPreUpdate");
        ImGui_ImplSDLGPU3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        if (auto *ui_mgr = entt::locator<ui_manager*>::value())
            ui_mgr->ui_new_frame(_safe.x, _safe.y, _safe.w, _safe.h);
    }
    else if (phase == step_phase::RENDER)
    {
        ZoneScopedN("Render");

        // ImGui draw data must be ready before we call PrepareDrawData
        if (auto *ui_mgr = entt::locator<ui_manager*>::value())
        {
            ui_mgr->draw_tool_windows();
            ui_mgr->draw_perf();
        }
        ImGui::Render();

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(_device);
        if (!cmd) return true;

        // PrepareDrawData uploads ImGui vertex/index buffers — must happen before render pass
        if (_has_ui)
            ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), cmd);

        const auto &layers = engine::instance().render_layers();

        // Determine clear color from first layer
        float cr = 0.f, cg = 0.f, cb = 0.f;
        if (!layers.empty() && layers.front().clear_bg)
        {
            cr = layers.front().clear_r;
            cg = layers.front().clear_g;
            cb = layers.front().clear_b;
        }

        // Collect sprite + mesh vertices across all layers for a single upload
        std::vector<sprite_vertex> sprite_verts;
        std::vector<mesh_vertex>   mesh_verts;
        std::vector<uint32_t>      mesh_indices;

        std::vector<draw_call> draw_calls;
        draw_calls.reserve(128);

        // Build vertex data + draw call list
        if (layers.empty())
        {
            auto dvp_it = _viewports.find(_default_vp);
            const viewport_entry &dvp = (dvp_it != _viewports.end())
                ? dvp_it->second
                : viewport_entry{ 0, 0, _wx, _wy, false, 0, 0, 0, 1 };
            const float fb_zoom = _fallback_camera.zoom > 0.f ? _fallback_camera.zoom : 1.f;
            // SDL_GPU NDC: y=-1 is bottom, y=+1 is top; world/viewport uses y-down → negate Y scale
            const glm::mat4 vp_mat = glm::scale(glm::mat4{1.f}, {2.f * fb_zoom / dvp.w, -2.f * fb_zoom / dvp.h, 1.f}) *
                                     glm::translate(glm::mat4{1.f}, {-_fallback_spatial.pos.x,
                                                                      -_fallback_spatial.pos.y, 0.f});
            auto &reg = engine::instance().default_scene().registry();
            _draw_scene(reg, vp_mat, 0xFFFFFFFF, dvp, sprite_verts, mesh_verts, mesh_indices, draw_calls);
        }
        else
        {
            for (const auto &layer : layers)
            {
                auto *sc = engine::instance().find_scene(layer.scene_id);
                if (!sc) continue;
                auto it = _viewports.find(layer.viewport);
                if (it == _viewports.end()) continue;
                const auto &vp = it->second;

                float cx = 0.f, cy = 0.f, zoom = 1.f;
                auto &reg = sc->registry();
                if (layer.camera != entt::null)
                {
                    if (auto *sp  = reg.try_get<cspatial>(layer.camera)) { cx = sp->pos.x; cy = sp->pos.y; }
                    if (auto *cam = reg.try_get<ccamera> (layer.camera)) { zoom = cam->zoom; }
                }
                // SDL_GPU NDC: y=-1 is bottom, y=+1 is top; world/viewport uses y-down → negate Y scale
                const glm::mat4 vp_mat =
                    glm::scale(glm::mat4{1.f}, {2.f * zoom / vp.w, -2.f * zoom / vp.h, 1.f}) *
                    glm::translate(glm::mat4{1.f}, {-cx, -cy, 0.f});

                _draw_scene(reg, vp_mat, layer.layer_mask, vp,
                            sprite_verts, mesh_verts, mesh_indices, draw_calls);
            }
        }

        // Upload vertex + index data via copy pass
        if (!sprite_verts.empty() || !mesh_verts.empty())
        {
            if (!sprite_verts.empty())
            {
                const uint32_t bytes = static_cast<uint32_t>(sprite_verts.size() * sizeof(sprite_vertex));
                auto* dst = static_cast<sprite_vertex*>(
                    SDL_MapGPUTransferBuffer(_device, _tbuf_sprite, true));
                memcpy(dst, sprite_verts.data(), bytes);
                SDL_UnmapGPUTransferBuffer(_device, _tbuf_sprite);
            }
            if (!mesh_verts.empty())
            {
                const uint32_t vbytes = static_cast<uint32_t>(mesh_verts.size() * sizeof(mesh_vertex));
                const uint32_t ibytes = static_cast<uint32_t>(mesh_indices.size() * sizeof(uint32_t));
                auto* vdst = static_cast<mesh_vertex*>(
                    SDL_MapGPUTransferBuffer(_device, _tbuf_mesh, true));
                memcpy(vdst, mesh_verts.data(), vbytes);
                SDL_UnmapGPUTransferBuffer(_device, _tbuf_mesh);

                auto* idst = static_cast<uint32_t*>(
                    SDL_MapGPUTransferBuffer(_device, _titbuf_mesh, true));
                memcpy(idst, mesh_indices.data(), ibytes);
                SDL_UnmapGPUTransferBuffer(_device, _titbuf_mesh);
            }

            SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
            if (!sprite_verts.empty())
            {
                SDL_GPUTransferBufferLocation src { _tbuf_sprite, 0 };
                SDL_GPUBufferRegion dst { _vbuf_sprite, 0,
                    static_cast<uint32_t>(sprite_verts.size() * sizeof(sprite_vertex)) };
                SDL_UploadToGPUBuffer(cp, &src, &dst, true);
            }
            if (!mesh_verts.empty())
            {
                SDL_GPUTransferBufferLocation src { _tbuf_mesh, 0 };
                SDL_GPUBufferRegion dst { _vbuf_mesh, 0,
                    static_cast<uint32_t>(mesh_verts.size() * sizeof(mesh_vertex)) };
                SDL_UploadToGPUBuffer(cp, &src, &dst, true);

                SDL_GPUTransferBufferLocation isrc { _titbuf_mesh, 0 };
                SDL_GPUBufferRegion idst { _ibuf_mesh, 0,
                    static_cast<uint32_t>(mesh_indices.size() * sizeof(uint32_t)) };
                SDL_UploadToGPUBuffer(cp, &isrc, &idst, true);
            }
            SDL_EndGPUCopyPass(cp);
        }

        // Upload any pending scene textures
        {
            SDL_GPUCopyPass* tcp = nullptr;
            for (auto &[rtex, entry] : _tex_cache)
            {
                if (entry.ready || !rtex->surf) continue;
                if (!tcp) tcp = SDL_BeginGPUCopyPass(cmd);
                _upload_scene_tex(cmd, tcp, rtex, entry);
            }
            if (tcp) SDL_EndGPUCopyPass(tcp);
        }

        // Acquire swapchain + begin render pass
        SDL_GPUTexture* swapchain = nullptr;
        if (!SDL_AcquireGPUSwapchainTexture(cmd, _win, &swapchain, nullptr, nullptr) || !swapchain)
        {
            SDL_SubmitGPUCommandBuffer(cmd);
            return true;
        }

        SDL_GPUColorTargetInfo ct {};
        ct.texture      = swapchain;
        ct.load_op      = SDL_GPU_LOADOP_CLEAR;
        ct.store_op     = SDL_GPU_STOREOP_STORE;
        ct.clear_color  = { cr, cg, cb, 1.f };

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);

        // Draw
        if (!draw_calls.empty())
        {
            SDL_GPUGraphicsPipeline* bound_pipe = nullptr;
            SDL_GPUTexture*          bound_tex  = nullptr;

            SDL_GPUBufferBinding vbind_sprite { _vbuf_sprite, 0 };
            SDL_GPUBufferBinding vbind_mesh   { _vbuf_mesh,   0 };
            SDL_GPUBufferBinding ibind_mesh   { _ibuf_mesh,   0 };

            for (const auto &dc : draw_calls)
            {
                SDL_PushGPUVertexUniformData(cmd, 0, &dc.viewproj, sizeof(dc.viewproj));

                if (dc.kind == draw_call::SPRITE)
                {
                    if (bound_pipe != _pipeline_sprite)
                    {
                        SDL_BindGPUGraphicsPipeline(pass, _pipeline_sprite);
                        SDL_BindGPUVertexBuffers(pass, 0, &vbind_sprite, 1);
                        bound_pipe = _pipeline_sprite;
                        bound_tex  = nullptr;
                    }
                    if (dc.tex != bound_tex)
                    {
                        SDL_GPUTextureSamplerBinding tsb { dc.tex, _default_sampler };
                        SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
                        bound_tex = dc.tex;
                    }
                    SDL_DrawGPUPrimitives(pass, dc.vert_count, 1, dc.vert_offset, 0);
                }
                else if (dc.kind == draw_call::MESH)
                {
                    if (bound_pipe != _pipeline_mesh2d)
                    {
                        SDL_BindGPUGraphicsPipeline(pass, _pipeline_mesh2d);
                        SDL_BindGPUVertexBuffers(pass, 0, &vbind_mesh, 1);
                        SDL_BindGPUIndexBuffer(pass, &ibind_mesh, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                        bound_pipe = _pipeline_mesh2d;
                        bound_tex  = nullptr;
                    }
                    SDL_DrawGPUIndexedPrimitives(pass, dc.idx_count, 1, dc.idx_offset, dc.vert_offset, 0);
                }
                else // MESH_TEX — sprite pipeline + indexed draw
                {
                    if (bound_pipe != _pipeline_sprite)
                    {
                        SDL_BindGPUGraphicsPipeline(pass, _pipeline_sprite);
                        bound_pipe = _pipeline_sprite;
                        bound_tex  = nullptr;
                    }
                    SDL_BindGPUVertexBuffers(pass, 0, &vbind_mesh, 1);
                    SDL_BindGPUIndexBuffer(pass, &ibind_mesh, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                    if (dc.tex != bound_tex)
                    {
                        SDL_GPUTextureSamplerBinding tsb { dc.tex, _default_sampler };
                        SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
                        bound_tex = dc.tex;
                    }
                    SDL_DrawGPUIndexedPrimitives(pass, dc.idx_count, 1, dc.idx_offset, dc.vert_offset, 0);
                }
            }
        }

        // ImGui
        if (_has_ui)
            ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), cmd, pass, nullptr);

        SDL_EndGPURenderPass(pass);
        SDL_SubmitGPUCommandBuffer(cmd);
        FrameMark;
    }
    return true;
}

// ---- scene drawing ----------------------------------------------------------------------------

void render_gpu::_upload_scene_tex(SDL_GPUCommandBuffer* cmd, SDL_GPUCopyPass* cp,
                                   rtexture* rtex, scene_tex_entry& entry)
{
    SDL_Surface* surf = rtex->surf;
    if (!surf) return;

    // Convert to RGBA8 if needed
    SDL_Surface* converted = nullptr;
    if (surf->format != SDL_PIXELFORMAT_RGBA32)
    {
        converted = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
        surf = converted;
    }

    SDL_GPUTextureCreateInfo tci {};
    tci.type          = SDL_GPU_TEXTURETYPE_2D;
    tci.format        = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.usage         = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width         = static_cast<uint32_t>(surf->w);
    tci.height        = static_cast<uint32_t>(surf->h);
    tci.layer_count_or_depth = 1;
    tci.num_levels    = 1;
    tci.sample_count  = SDL_GPU_SAMPLECOUNT_1;
    entry.tex = SDL_CreateGPUTexture(_device, &tci);
    entry.w   = surf->w;
    entry.h   = surf->h;

    const uint32_t row_bytes  = static_cast<uint32_t>(surf->w * 4);
    const uint32_t tex_bytes  = row_bytes * static_cast<uint32_t>(surf->h);
    auto* tbuf = _create_transfer_buffer(_device, SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, tex_bytes);
    auto* dst  = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(_device, tbuf, false));
    if (static_cast<int>(row_bytes) == surf->pitch)
    {
        memcpy(dst, surf->pixels, tex_bytes);
    }
    else
    {
        const auto* src = static_cast<const uint8_t*>(surf->pixels);
        for (int row = 0; row < surf->h; ++row)
            memcpy(dst + row * row_bytes, src + row * surf->pitch, row_bytes);
    }
    SDL_UnmapGPUTransferBuffer(_device, tbuf);

    SDL_GPUTextureTransferInfo src {};
    src.transfer_buffer = tbuf;
    src.offset          = 0;
    src.pixels_per_row  = static_cast<uint32_t>(surf->w);
    src.rows_per_layer  = static_cast<uint32_t>(surf->h);

    SDL_GPUTextureRegion rgn {};
    rgn.texture = entry.tex;
    rgn.w = static_cast<uint32_t>(surf->w);
    rgn.h = static_cast<uint32_t>(surf->h);
    rgn.d = 1;

    SDL_UploadToGPUTexture(cp, &src, &rgn, false);

    // Transfer buffer will be released after submit — schedule via a fence or just release now
    // (safe: upload is recorded into the command buffer, not executed yet, but the data was copied)
    SDL_ReleaseGPUTransferBuffer(_device, tbuf);

    if (converted) SDL_DestroySurface(converted);
    SDL_DestroySurface(rtex->surf);
    rtex->surf = nullptr;

    entry.ready = true;
}

void render_gpu::_draw_scene(entt::registry& reg, const glm::mat4& viewproj,
                              uint32_t layer_mask, const viewport_entry& /*vp*/,
                              std::vector<sprite_vertex>& sv, std::vector<mesh_vertex>& mv,
                              std::vector<uint32_t>& mi,
                              std::vector<draw_call>& dcs)
{
    reg.sort<cspatial>([](const cspatial &a, const cspatial &b) { return a.pos[2] > b.pos[2]; });

    for (auto [id, spatial] : reg.view<const cspatial>().each())
    {
        const auto *lyr = reg.try_get<clayers>(id);
        if (!((lyr ? lyr->mask : clayers::MASK_DEFAULT) & layer_mask)) continue;

        if (auto *sprite = reg.try_get<const csprite>(id))
        {
            if (!sprite->visible || !sprite->spr) continue;
            auto *tex = sprite->spr->tex.get();
            if (!tex) continue;

            auto &entry = _tex_cache[tex];
            if (!entry.ready && !tex->surf) continue;
            if (!entry.ready) continue;

            const glm::vec4 &csr = sprite->current_source_rect;
            glm::vec2 dims = sprite->spr->dims;

            const float tex_w = static_cast<float>(entry.w);
            const float tex_h = static_cast<float>(entry.h);

            if (dims == glm::vec2{-1.f, -1.f})
                dims = csr.z > 0.f ? glm::vec2{csr.z, csr.w} : glm::vec2{tex_w, tex_h};

            float u0 = 0.f, v0 = 0.f, u1 = 1.f, v1 = 1.f;
            if (csr.z > 0.f && tex_w > 0.f && tex_h > 0.f)
            {
                u0 = csr.x / tex_w;           v0 = csr.y / tex_h;
                u1 = (csr.x + csr.z) / tex_w; v1 = (csr.y + csr.w) / tex_h;
            }

            const float ql = -sprite->spr->anchor.x * dims.x;
            const float qt = -sprite->spr->anchor.y * dims.y;

            // Snap in local space before the world transform
            const auto snap = [&](float v) { return sprite->pixel_snap ? std::roundf(v) : v; };
            const float lx0 = snap(ql),         ly0 = snap(qt);
            const float lx1 = snap(ql + dims.x), ly1 = snap(qt + dims.y);

            // Transform to world space on the CPU; viewproj is applied in the shader
            auto world_xform = [&](float lx, float ly) -> glm::vec2 {
                const glm::vec4 w = spatial.world * glm::vec4{lx, ly, 0.f, 1.f};
                return { w.x, w.y };
            };
            const glm::vec2 tl = world_xform(lx0, ly0);
            const glm::vec2 tr = world_xform(lx1, ly0);
            const glm::vec2 bl = world_xform(lx0, ly1);
            const glm::vec2 br = world_xform(lx1, ly1);

            const auto &c = sprite->color;
            const uint32_t base = static_cast<uint32_t>(sv.size());
            sv.push_back({ tl.x, tl.y, u0, v0, c.r, c.g, c.b, c.a });
            sv.push_back({ tr.x, tr.y, u1, v0, c.r, c.g, c.b, c.a });
            sv.push_back({ bl.x, bl.y, u0, v1, c.r, c.g, c.b, c.a });
            sv.push_back({ tr.x, tr.y, u1, v0, c.r, c.g, c.b, c.a });
            sv.push_back({ br.x, br.y, u1, v1, c.r, c.g, c.b, c.a });
            sv.push_back({ bl.x, bl.y, u0, v1, c.r, c.g, c.b, c.a });

            // Merge consecutive calls with same texture and same viewproj (same layer)
            if (!dcs.empty() && dcs.back().kind == draw_call::SPRITE
                && dcs.back().tex == entry.tex && dcs.back().viewproj == viewproj)
            {
                dcs.back().vert_count += 6;
            }
            else
            {
                dcs.push_back({ draw_call::SPRITE, entry.tex, viewproj, base, 6, 0, 0 });
            }
        }
        else if (auto *mesh = reg.try_get<const cmesh2d>(id))
        {
            if (!mesh->visible || !mesh->geom || mesh->geom->empty()) continue;

            // Resolve mesh texture if present
            SDL_GPUTexture* mesh_gpu_tex = nullptr;
            if (mesh->tex)
            {
                auto *rtex = mesh->tex.get();
                auto &entry = _tex_cache[rtex];
                if (!entry.ready && rtex->surf)
                {
                    // Will be uploaded in the pre-pass next frame; skip this frame
                }
                if (entry.ready) mesh_gpu_tex = entry.tex;
            }

            const auto &geom   = *mesh->geom;
            const uint32_t vbase = static_cast<uint32_t>(mv.size());
            const uint32_t ibase = static_cast<uint32_t>(mi.size());

            for (const auto &v : geom.vertices)
            {
                const float lx = mesh->pixel_snap ? std::roundf(v.pos.x) : v.pos.x;
                const float ly = mesh->pixel_snap ? std::roundf(v.pos.y) : v.pos.y;
                // Transform to world space; viewproj applied in shader
                const glm::vec4 w = spatial.world * glm::vec4{lx, ly, 0.f, 1.f};
                mv.push_back({ w.x, w.y, v.uv.x, v.uv.y, v.color.r, v.color.g, v.color.b, v.color.a });
            }

            uint32_t idx_count = 0;
            if (!geom.indices.empty())
            {
                for (int i : geom.indices) mi.push_back(static_cast<uint32_t>(i));
                idx_count = static_cast<uint32_t>(geom.indices.size());
            }
            else
            {
                const uint32_t vc = static_cast<uint32_t>(geom.vertices.size());
                for (uint32_t i = 0; i < vc; ++i) mi.push_back(i);
                idx_count = vc;
            }

            const auto kind = mesh_gpu_tex ? draw_call::MESH_TEX : draw_call::MESH;
            dcs.push_back({ kind, mesh_gpu_tex, viewproj, vbase, 0, ibase, idx_count });
        }
    }
}

// ---- event ------------------------------------------------------------------------------------

bool render_gpu::event(SDL_Event* evt)
{
    ImGui_ImplSDL3_ProcessEvent(evt);

    if (evt->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED &&
        evt->window.windowID == SDL_GetWindowID(_win))
    {
        _wx = evt->window.data1;
        _wy = evt->window.data2;
        if (!_default_vp_owned && _default_vp != VIEWPORT_INVALID)
            update_viewport(_default_vp, 0, 0, _wx, _wy);
        log::info("[render_gpu] resized to %dx%d", _wx, _wy);
    }
    else if (evt->type == SDL_EVENT_WINDOW_RESIZED &&
             evt->window.windowID == SDL_GetWindowID(_win))
    {
        SDL_GetWindowSafeArea(_win, &_safe);
    }

    if (evt->type == SDL_EVENT_KEY_DOWN && evt->key.scancode == SDL_SCANCODE_F11)
        SDL_SetWindowFullscreen(_win, !(SDL_GetWindowFlags(_win) & SDL_WINDOW_FULLSCREEN));

    return true;
}

// ---- renderer_service -------------------------------------------------------------------------

bool render_gpu::get_2d_extents(renderer_service::extents_2d &extents)
{
    const auto &layers = engine::instance().render_layers();
    if (!layers.empty())
    {
        const auto &layer = layers.front();
        auto *sc = engine::instance().find_scene(layer.scene_id);
        auto it  = _viewports.find(layer.viewport);
        if (sc && it != _viewports.end())
        {
            auto &reg = sc->registry();
            auto &vp  = it->second;
            float cx = 0.f, cy = 0.f, zoom = 1.f;
            if (layer.camera != entt::null)
            {
                if (auto *sp  = reg.try_get<cspatial>(layer.camera)) { cx = sp->pos.x; cy = sp->pos.y; }
                if (auto *cam = reg.try_get<ccamera> (layer.camera)) { zoom = cam->zoom; }
            }
            float span_x = vp.w / zoom, span_y = vp.h / zoom;
            extents = { vp.w, vp.h, span_x, span_y,
                cx - span_x * 0.5f, cy - span_y * 0.5f,
                cx + span_x * 0.5f, cy + span_y * 0.5f,
                _scale, vp.x, vp.y };
            return true;
        }
    }
    auto dvp_it = _viewports.find(_default_vp);
    int dvp_w = (dvp_it != _viewports.end()) ? dvp_it->second.w : _wx;
    int dvp_h = (dvp_it != _viewports.end()) ? dvp_it->second.h : _wy;
    int dvp_x = (dvp_it != _viewports.end()) ? dvp_it->second.x : 0;
    int dvp_y = (dvp_it != _viewports.end()) ? dvp_it->second.y : 0;
    float zoom = _fallback_camera.zoom > 0.f ? _fallback_camera.zoom : 1.f;
    float cx = _fallback_spatial.pos.x, cy = _fallback_spatial.pos.y;
    float span_x = dvp_w / zoom, span_y = dvp_h / zoom;
    extents = { dvp_w, dvp_h, span_x, span_y,
        cx - span_x * 0.5f, cy - span_y * 0.5f,
        cx + span_x * 0.5f, cy + span_y * 0.5f,
        _scale, dvp_x, dvp_y };
    return true;
}

viewport_handle render_gpu::create_viewport(int x, int y, int w, int h,
                                             bool clear, float r, float g, float b, float a)
{
    viewport_handle h_ = _next_vp_handle++;
    _viewports[h_] = { x, y, w, h, clear, r, g, b, a };
    return h_;
}

void render_gpu::update_viewport(viewport_handle vp, int x, int y, int w, int h)
{
    auto it = _viewports.find(vp);
    if (it == _viewports.end()) return;
    it->second.x = x; it->second.y = y;
    it->second.w = w; it->second.h = h;
    if (vp == _default_vp) _default_vp_owned = true;
}

void render_gpu::destroy_viewport(viewport_handle vp)
{
    _viewports.erase(vp);
}

void render_gpu::reset_default_viewport()
{
    _default_vp_owned = false;
    if (_default_vp != VIEWPORT_INVALID)
        update_viewport(_default_vp, 0, 0, _wx, _wy);
}

// ---- texture service --------------------------------------------------------------------------

renderer_service::texture_handle render_gpu::create_texture(int w, int h)
{
    SDL_GPUTextureCreateInfo tci {};
    tci.type   = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.usage  = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width  = static_cast<uint32_t>(w);
    tci.height = static_cast<uint32_t>(h);
    tci.layer_count_or_depth = 1;
    tci.num_levels   = 1;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;

    SDL_GPUTexture* tex = SDL_CreateGPUTexture(_device, &tci);
    if (!tex) return nullptr;

    const uint32_t bytes = static_cast<uint32_t>(w * h * 4);
    SDL_GPUTransferBuffer* tbuf = _create_transfer_buffer(_device, SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, bytes);
    _service_textures[tex] = { tex, tbuf, w, h };
    return tex;
}

void render_gpu::update_texture(texture_handle handle, const void* pixels, int pitch)
{
    auto it = _service_textures.find(handle);
    if (it == _service_textures.end()) return;
    auto &e = it->second;

    auto* dst = SDL_MapGPUTransferBuffer(_device, e.transfer_buf, true);
    const uint8_t* src = static_cast<const uint8_t*>(pixels);
    for (int row = 0; row < e.h; ++row)
        memcpy(static_cast<uint8_t*>(dst) + row * e.w * 4, src + row * pitch, e.w * 4);
    SDL_UnmapGPUTransferBuffer(_device, e.transfer_buf);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(_device);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTextureTransferInfo tsrc { e.transfer_buf, 0,
        static_cast<uint32_t>(e.w), static_cast<uint32_t>(e.h) };
    SDL_GPUTextureRegion rgn {};
    rgn.texture = e.tex;
    rgn.w = static_cast<uint32_t>(e.w);
    rgn.h = static_cast<uint32_t>(e.h);
    rgn.d = 1;
    SDL_UploadToGPUTexture(cp, &tsrc, &rgn, true);

    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
}

void render_gpu::destroy_texture(texture_handle handle)
{
    auto it = _service_textures.find(handle);
    if (it == _service_textures.end()) return;
    SDL_WaitForGPUIdle(_device);
    SDL_ReleaseGPUTexture(_device, it->second.tex);
    SDL_ReleaseGPUTransferBuffer(_device, it->second.transfer_buf);
    _service_textures.erase(it);
}

// ---- picker (identical logic to render_simple) ------------------------------------------------

entt::entity render_gpu::pick(const render_layer &layer, float vp_x, float vp_y)
{
    auto *sc = engine::instance().find_scene(layer.scene_id);
    if (!sc) return entt::null;
    auto it = _viewports.find(layer.viewport);
    if (it == _viewports.end()) return entt::null;
    const auto &vp = it->second;

    const float win_x = vp_x + vp.x;
    const float win_y = vp_y + vp.y;
    const float vp_cx = vp.x + vp.w * 0.5f;
    const float vp_cy = vp.y + vp.h * 0.5f;

    float cam_cx = 0.f, cam_cy = 0.f, zoom = 1.f;
    auto &reg = sc->registry();
    if (layer.camera != entt::null)
    {
        if (auto *sp  = reg.try_get<cspatial>(layer.camera)) { cam_cx = sp->pos.x; cam_cy = sp->pos.y; }
        if (auto *cam = reg.try_get<ccamera> (layer.camera)) { zoom = cam->zoom; }
    }

    const float wx = (win_x - vp_cx) / zoom + cam_cx;
    const float wy = (win_y - vp_cy) / zoom + cam_cy;

    entt::entity best = entt::null;
    float best_z = std::numeric_limits<float>::max();

    for (auto [id, spatial] : reg.view<const cspatial>().each())
    {
        const auto *lyr_comp = reg.try_get<clayers>(id);
        if (!((lyr_comp ? lyr_comp->mask : clayers::MASK_DEFAULT) & layer.layer_mask)) continue;

        if (auto *sprite = reg.try_get<const csprite>(id))
        {
            if (!sprite->visible || !sprite->spr) continue;
            glm::vec2 dims = sprite->spr->dims;
            if (dims == glm::vec2{-1.f, -1.f})
            {
                const glm::vec4 &csr = sprite->current_source_rect;
                if (csr.z > 0.f) dims = { csr.z, csr.w };
                else { auto &e = _tex_cache[sprite->spr->tex.get()]; if (e.ready) dims = {(float)e.w, (float)e.h}; else continue; }
            }
            const glm::vec4 local = glm::inverse(spatial.world) * glm::vec4{wx, wy, 0.f, 1.f};
            const float ql = -sprite->spr->anchor.x * dims.x, qt = -sprite->spr->anchor.y * dims.y;
            if (local.x >= ql && local.x <= ql + dims.x && local.y >= qt && local.y <= qt + dims.y)
                if (spatial.pos.z < best_z) { best_z = spatial.pos.z; best = id; }
        }
        else if (auto *mesh = reg.try_get<const cmesh2d>(id))
        {
            if (!mesh->visible || !mesh->geom || mesh->geom->empty()) continue;
            const glm::vec4 local4 = glm::inverse(spatial.world) * glm::vec4{wx, wy, 0.f, 1.f};
            const glm::vec2 lp { local4.x, local4.y };
            const auto &geom = *mesh->geom;
            const auto &verts = geom.vertices;
            auto tri_hit = [&](int i0, int i1, int i2) {
                const glm::vec2 a{verts[i0].pos}, b{verts[i1].pos}, c{verts[i2].pos};
                const float d1 = (lp.x-b.x)*(a.y-b.y) - (a.x-b.x)*(lp.y-b.y);
                const float d2 = (lp.x-c.x)*(b.y-c.y) - (b.x-c.x)*(lp.y-c.y);
                const float d3 = (lp.x-a.x)*(c.y-a.y) - (c.x-a.x)*(lp.y-a.y);
                return !((d1<0||d2<0||d3<0) && (d1>0||d2>0||d3>0));
            };
            bool hit = false;
            if (!geom.indices.empty())
                for (size_t i = 0; i+2 < geom.indices.size() && !hit; i += 3)
                    hit = tri_hit(geom.indices[i], geom.indices[i+1], geom.indices[i+2]);
            else
                for (size_t i = 0; i+2 < verts.size() && !hit; i += 3)
                    hit = tri_hit((int)i, (int)i+1, (int)i+2);
            if (hit && spatial.pos.z < best_z) { best_z = spatial.pos.z; best = id; }
        }
        else if (auto *emit = reg.try_get<const cparticle_emitter>(id))
        {
            float radius = 24.f;
            if (emit->res) radius = std::max(24.f, glm::length(emit->res->emitter.pos_variance));
            const float dx = wx - spatial.pos.x, dy = wy - spatial.pos.y;
            if (dx*dx + dy*dy <= radius*radius && spatial.pos.z < best_z)
                { best_z = spatial.pos.z; best = id; }
        }
        else
        {
            constexpr float HR = 8.f;
            const float dx = wx - spatial.pos.x, dy = wy - spatial.pos.y;
            if (dx*dx + dy*dy <= HR*HR && spatial.pos.z < best_z)
                { best_z = spatial.pos.z; best = id; }
        }
    }
    return best;
}

// ---- misc -------------------------------------------------------------------------------------

void render_gpu::on_scene_change() {}

void render_gpu::cam_2d_setup(float cx, float cy, float wmax, float hmax)
{
    _fallback_spatial.pos = { cx, cy, 0.f };
    _fallback_camera.wmax = wmax;
    _fallback_camera.hmax = hmax;
    float scale_x = _wx / wmax;
    float scale_y = _wy / hmax;
    _fallback_camera.zoom = std::min(scale_x, scale_y);
}

void render_gpu::set_clear_color(float r, float g, float b)
{
    auto it = _viewports.find(_default_vp);
    if (it != _viewports.end())
    {
        it->second.r = r;
        it->second.g = g;
        it->second.b = b;
    }
}

// ---- RTTI -------------------------------------------------------------------------------------

extern "C" void _rtti_init_render_gpu()
{
    entt::meta_factory<nb::render_gpu>{}
        .type("render_gpu"_hs)
        .custom<rtti::type_info>(rtti::type_info{"render_gpu", rtti::TYPE_CLASS_SYSTEM})
        .base<nb::system>()
        .func<&nb::render_gpu::window_width> ("window_width"_hs) .custom<rtti::func_info>(rtti::func_info{"window_width"})
        .func<&nb::render_gpu::window_height>("window_height"_hs).custom<rtti::func_info>(rtti::func_info{"window_height"})
        .func<&nb::render_gpu::display_scale>("display_scale"_hs).custom<rtti::func_info>(rtti::func_info{"display_scale"})
        .func<&nb::render_gpu::default_viewport>("default_viewport"_hs).custom<rtti::func_info>(rtti::func_info{"default_viewport"});
    entt::meta_factory<std::shared_ptr<nb::render_gpu>>{rtti::ctx_systems()}
        .type("render_gpu_shared"_hs)
        .ctor<&rtti::shared_ptr_builder<nb::render_gpu>>()
        .conv<std::shared_ptr<nb::system>>();

    cspatial::_ensure_rtti();
    cstructure::_ensure_rtti();
    csprite::_ensure_rtti();
    cmesh2d::_ensure_rtti();
    cparticle_emitter::_ensure_rtti();
    ccamera::_ensure_rtti();
    clayers::_ensure_rtti();
}
