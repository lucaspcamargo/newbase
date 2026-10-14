#include "newbase/render/material.hpp"
#include "newbase/render/pass.hpp"
#include "newbase/render/pipeline.hpp"
#include "newbase/render/mat/std_2d.hpp"
#include "newbase/render/vertex.hpp"
#include <newbase/sys/render_gpu/render_gpu.hpp>
#include <newbase/sys/render_gpu/buffers.hpp>
#include <newbase/sys/render_gpu/pipelines.hpp>
#include <newbase/sys/render_gpu/pass_control.hpp>
#include <newbase/sys/render_gpu/shaders.hpp>
#include <newbase/sys/render_gpu/samplers.hpp>
#include <newbase/sys/render_gpu/targets.hpp>
#include <newbase/sys/render_gpu/textures.hpp>
#include <newbase/sys/render_gpu/util2d.hpp>
#include <newbase/engine.hpp>
#include <newbase/scene.hpp>
#include <newbase/layer.hpp>
#include <newbase/geom/picker2d.hpp>
#include <newbase/geom/picker3d.hpp>
#include <newbase/render/batcher2d.hpp>
#include <newbase/render/collector2d.hpp>
#include <newbase/render/collector3d.hpp>
#include <newbase/render/shader.hpp>
#include <newbase/render/types.hpp>
#include <newbase/render/window.hpp>
#include <newbase/res/sprite.hpp>
#include <newbase/res/texture.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/services/ui_manager.hpp>
#include <newbase/ui/imgui_nb.hpp>
#include <newbase/log.hpp>

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <unordered_map>


using namespace nb;
using entt::operator""_hs;


struct nb::render_gpu_p
{
    render::window rwin {};
    render::viewport_t ui_vp {};
    bool debug {false};

    // GPU
    SDL_GPUDevice *gdev {nullptr};
    SDL_GPUCommandBuffer *cmds {nullptr};

    // GPU Resource Managers
    gpu::buffer_manager buffers {};
    gpu::shader_manager shaders {};
    gpu::texture_manager textures {};
    gpu::sampler_cache samplers {};
    gpu::target_manager targets {textures};
    gpu::pass_control passes {targets};
    gpu::pipeline_cache pipelines {targets};

    // 3D rendering data
    render::collector3d collector_3d {};

    // 2D rendering data
    render::batcher2d batcher;
    render::collector2d collector_2d;
    render::collector2d::results collector_results;
    gpu::buffers2d buffers2d;

    // UI integration
    bool has_ui {false};
    imgui_nb imgui;
    render::batcher2d::sync_id_t ui_sync;

    // picking
    geom::picker_2d picker;
    geom::picker_3d picker_3d;

    // TEST
    SDL_GPUBuffer *m_cube_v;
    SDL_GPUBuffer *m_cube_i;
    std::shared_ptr<rshader> m_cube_frag;
    std::shared_ptr<rshader> m_cube_vert;
};



// intialization and teardown

render_gpu::render_gpu()
{
    _d = std::make_unique<render_gpu_p>();
    entt::locator<renderer_service*>::emplace(this);
    entt::locator<picker_service*>::emplace(this);
    log::info("[render_gpu] constructed");
}


render_gpu::~render_gpu()
{
    log::info("[render_gpu] destroying");
    if (_d->has_ui)
    {
        _d->imgui.teardown();
        ui_manager* ui_mgr = entt::locator<ui_manager*>::value();
        if(ui_mgr)
        {
            ui_mgr->ui_destroy();
        }
        else
            log::warn("[render_gpu] could not locate ui service for teardown");
    }

    if (_d->gdev)
    {
        SDL_WaitForGPUIdle(_d->gdev);

        // finalize GPU resources managers
        _d->targets.teardown();
        _d->textures.teardown(_d->gdev);
        _d->shaders.teardown(_d->gdev, _d->pipelines);
        _d->buffers.teardown(_d->gdev);

        // release pipelines
        _d->pipelines.clear(_d->gdev);
        _d->samplers.teardown(_d->gdev);

        // relase streaming 2d geometry buffers
        _d->buffers2d.teardown(_d->gdev);

        // decouple gpu device from window
        if (_d->rwin.get())
            SDL_ReleaseWindowFromGPUDevice(_d->gdev, _d->rwin.get());

        // destroy gpu device
        SDL_DestroyGPUDevice(_d->gdev);
    }

    // release our private data
    // render::window is destroyed in tandem
    _d.reset();

    log::info("[render_gpu] destroyed");
}

bool render_gpu::init(ryml::ConstNodeRef cfg)
{

    // setup default debug mode according to build config
    // can be overriden in config yaml
    #ifdef NDEBUG
    _d->debug = false;
    #else
    _d->debug = true;
    #endif
    if(cfg.has_child("debug"))
    {
        cfg["debug"] >> _d->debug;
    }

    log::info("[render_gpu] init");

    // probe available drivers if in debug mode
    if(_d->debug)
    {
        std::string msg {"[render_gpu] available gpu drivers: "};
        const auto num_drivers = SDL_GetNumGPUDrivers();
        for(int i = 0; i < num_drivers; i++)
        {
            msg += SDL_GetGPUDriver(i);
            msg += " ";
        }
        log::info(msg.c_str());
    }


    log::info("[render_gpu] current video driver: %s", SDL_GetCurrentVideoDriver());

    if(!_d->rwin.create(cfg))
    {
        log::error("[render_gpu] window creation failed");
        return false;
    }
    _d->rwin.show();
    _d->ui_vp = {0, 0, _d->rwin.width(), _d->rwin.height()};

    // TODO what about metal? webgpu in the future?
    // On Windows, we are not going to support DX12 yet, sticking with Vulkan
    _d->gdev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, _d->debug, nullptr);
    if (!_d->gdev)
    {
        log::error("[render_gpu] SDL_CreateGPUDevice: %s", SDL_GetError());
        return false;
    }
    log::info("[render_gpu] GPU driver: %s", SDL_GetGPUDeviceDriver(_d->gdev));

    if (!SDL_ClaimWindowForGPUDevice(_d->gdev, _d->rwin.get()))
    {
        log::error("[render_gpu] SDL_ClaimWindowForGPUDevice: %s", SDL_GetError());
        return false;
    }

    _d->rwin.set_icon("_nb_core/icons/icon_192.png"_hs);

    _d->buffers.init(_d->gdev);
    _d->textures.init(_d->gdev, _d->debug);
    _d->shaders.init(_d->gdev);
    _d->buffers2d.init(_d->gdev);
    _d->targets.init(_d->rwin.width(), _d->rwin.height(), true, false); // TODO configurable depth?

    // init UI
    ui_manager* ui_mgr = entt::locator<ui_manager*>::value();
    if(ui_mgr)
    {
        _d->has_ui = ui_mgr->ui_init();
    }
    else
        log::warn("[render_gpu] could not init ui via ui_manager service");
    if(_d->has_ui)
    {
        log::info("[render_gpu] ui init");
        _d->imgui.init(_d->rwin);
        ui_mgr->ui_init_finish(_d->rwin.ui_scale());
        // connect textures registered in ui manager to our imgui backend
        _d->imgui.set_texture_lookup_callback([ui_mgr](uint64_t id){
            return ui_mgr->texture_get(id);
        });
    }
    else
    {
        log::warn("[render_gpu] no ui, not initializing ImGui renderer");
    }

    return true;
}

void render_gpu::shutdown()
{
    // release all of our own shared resource references
    _d->batcher.clear();
    _d->collector_2d.clear();
    _d->targets.clear();

    // textures, shaders and buffers only hold gpu resources
    // the resources themselves need to be destroyed for cleanup
}


bool render_gpu::step(nb::step_phase phase)
{
    if(phase == step_phase::PREPARE)
    {
        // Start UI frame
        ui_manager* ui_mgr = entt::locator<ui_manager*>::value();
        if(ui_mgr)
        {
            _d->imgui.new_frame();
            const auto safe = _d->rwin.safe_area();
            ui_mgr->ui_new_frame(safe.x, safe.y, safe.w, safe.h);
        }
    }
    else if(phase == step_phase::UI_RENDER)
    {
        if(_d->has_ui)
        {
            ui_manager* ui_mgr = entt::locator<ui_manager*>::value();
            ui_mgr->draw_tool_windows();
            ui_mgr->draw_perf();
        }
    }
    else if(phase == step_phase::PRE_UPDATE)
    {
        // ui (except overlays) is ready
        // update targets and viewports geometry for accurate data in udpdate

        if(_d->has_ui)
        {
            ui_manager* ui_mgr = entt::locator<ui_manager*>::value();
            auto central = ui_mgr->update_viewports();
            _d->ui_vp = {central.x, central.y, central.z, central.w};
        }

        _d->targets.update_sizes(_d->ui_vp);

    }
    else if(phase == step_phase::PRE_RENDER)
    {
        // free gpu resources marked for deletion
        _d->textures.cleanup(_d->gdev);
        _d->shaders.cleanup(_d->gdev, _d->pipelines);
        _d->buffers.cleanup(_d->gdev);

        // clear 2d batcher and collector for upcoming render ops
        _d->batcher.clear();
        _d->collector_2d.clear();

        // do another render target resize pass
        // we could have new targets and viewport added during the update cycle
        auto swapchain_format = SDL_GetGPUSwapchainTextureFormat(_d->gdev, _d->rwin.get());
        _d->targets.update_default(nullptr, swapchain_format, _d->rwin.width(), _d->rwin.height());
        _d->targets.update_sizes(_d->ui_vp);

        // batch 2D geometry for render layer
        // opportunistically, we save the id of the empty sync point
        // for ui rendering
        _d->ui_sync = _d->collector_2d.collect_all_layers(_d->batcher, _d->collector_results, render::collector2d::clear_quad_mode::CLEAR_QUAD_ALWAYS);
        // TODO use regular clears for first layers of targets

        // batch 2D ui geometry
        // layer collection has already created a sync point
        // overlays are drawn here after update, for accurate state representation
        if(_d->has_ui)
        {
            ui_manager* ui_mgr = entt::locator<ui_manager*>::value();
            ui_mgr->draw_overlays();
            _d->imgui.render_flush(_d->batcher);
        }

        // 2d geometry is ready for upload

        // 3d render command collection
        _d->collector_3d.clear();
        const auto &layers = engine::instance().render_layers();
        for(const auto &l : layers)
        {
            if(!l.three_dee)
                continue;
            render::pass_target_info pt_info;
            if(!_d->targets.target_info_get(l.target_id, pt_info))
            {
                log::warn("[render_gpu] could not get pass target info: %u", (unsigned)l.target_id);
                continue;
            }
            _d->collector_3d.collect(l, pt_info);
        }
    }
    else if(phase == step_phase::RENDER)
    {
        _render();
    }

    return true;
}

bool render_gpu::event( SDL_Event * evt)
{
    if(_d->has_ui)
    {
        _d->imgui.event(evt);
    }

    if(_d->rwin.event(evt))
    {
        if(!_d->has_ui)
        {
            // if we have no UI, update viewport when window changes
            // otherwise, this will get done in PRE_RENDER phase from ui info
            _d->ui_vp = {0, 0, _d->rwin.width(), _d->rwin.height()};
        }
    }

    return true;
}


// Rendering

void render_gpu::_render()
{
    assert(_d->gdev);

    SDL_Window *window = _d->rwin.get();
    SDL_GPUDevice *device = _d->gdev;

    // acquire command buffer
    assert(!(_d->cmds) && "[render_gpu] leftover command buffer!");
    _d->cmds = SDL_AcquireGPUCommandBuffer(device);
    if (!_d->cmds) {
        SDL_Log("Failed to acquire command buffer: %s", SDL_GetError());
        return;
    }


    // ensure shaders are ready
    {
        auto shader_begin = _d->collector_3d.staging_shaders().begin();
        auto shader_end = shader_begin + _d->collector_3d.staging_shader_count();
        _d->shaders.prepare_multiple(_d->gdev, shader_begin, shader_end);

        static bool prepared_2d = false;
        if(!prepared_2d)
        {
            render::mat_std_2d *mat_controller = (render::mat_std_2d*)
                render::material_controller::get_controller(render::material_type::STANDARD_2D);
            auto all_2d = mat_controller->get_all_shaders();
            _d->shaders.prepare_multiple(_d->gdev, all_2d.begin(), all_2d.end());
            prepared_2d = true;
        }
    }

    // copy necessary data
    {
        gpu::copy_scope cpy {_d->cmds};

        // 3D resources
        auto mesh_begin = _d->collector_3d.staging_meshes().begin();
        auto mesh_end = mesh_begin + _d->collector_3d.staging_mesh_count();
        _d->buffers.prepare_multiple(_d->gdev, _d->cmds, mesh_begin, mesh_end, cpy);
        auto tex_begin = _d->collector_3d.staging_textures().begin();
        auto tex_end = tex_begin + _d->collector_3d.staging_texture_count();
        _d->textures.prepare_multiple(_d->gdev, _d->cmds, tex_begin, tex_end, cpy);
        // 2D resouces
        _d->buffers2d.upload(_d->gdev, _d->cmds, _d->batcher, cpy);
        _d->textures.prepare_multiple(_d->gdev, _d->cmds, _d->batcher.data().tex.begin(), _d->batcher.data().tex.end(), cpy);
    }
    _d->textures.generate_mipmaps(_d->cmds);

    // acquire the swapchain texture for this frame
    SDL_GPUTexture *swapchain = NULL;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(_d->cmds, window, &swapchain, NULL, NULL)) {
        log::warn("[render_gpu] failed to acquire swapchain texture: %s", SDL_GetError());
        return;
    }

    if (swapchain == NULL)
        return; // window must be hidden


    // update target textures
    auto swapchain_format = SDL_GetGPUSwapchainTextureFormat(_d->gdev, _d->rwin.get());
    _d->targets.update_default(swapchain, swapchain_format, _d->rwin.width(), _d->rwin.height());
    _d->targets.commit_textures(_d->gdev);

    // global window 2d projection
    auto proj_2d = gpu::make_ortho_2d(_d->rwin.width(), _d->rwin.height());

    // go over layers and submit draw commands
    int layer_mark_3d = 0;
    for(const auto &layer : engine::instance().render_layers())
    {
        if(!_d->passes.bind_target(layer, _d->cmds))
            continue;

        auto sync_it = _d->collector_results.sync_mapping.find(layer.order);
        if(sync_it == _d->collector_results.sync_mapping.end())
            continue; // layer was not batched ?! not 2d?!

        // setting 2D GPU viewport
        SDL_GPUViewport viewport = { 0.0f, 0.0f, static_cast<float>(_d->rwin.width()), static_cast<float>(_d->rwin.height()), 0.0f, 1.0f };
        SDL_SetGPUViewport(_d->passes.current(), &viewport);

        // render batches
        auto clip = render::clip_t { 0, 0, (float)_d->rwin.width(), (float)_d->rwin.height() };
        _render_2d_batches(sync_it->second, proj_2d, clip);

        // we might decide later that 2d rendering happens on top of 3d rendering
        if(layer.three_dee)
            _render_3d_layer(layer, layer_mark_3d++);

    }

    // render ui batched geometry
    if(_d->has_ui)
        _render_ui();

    _d->passes.unbind();
    // submit
    SDL_SubmitGPUCommandBuffer(_d->cmds);
    _d->cmds = nullptr;
}

void render_gpu::_render_ui()
{
    // bind main target if needed
    const render_layer ui_l {
        0,
        0,
        entt::null,
        999999,
        {0, 0, _d->rwin.width(), _d->rwin.height() },
        true,
        false,
        render::TARGET_DEFAULT
    };

    if(!_d->passes.bind_target(ui_l, _d->cmds))
        return;

    // setting GPU viewport
    SDL_GPUViewport viewport = { 0.0f, 0.0f, static_cast<float>(_d->rwin.width()), static_cast<float>(_d->rwin.height()), 0.0f, 1.0f };
    SDL_SetGPUViewport(_d->passes.current(), &viewport);

    auto clip = render::clip_t { 0, 0, (float)_d->rwin.width(), (float)_d->rwin.height() };
    auto proj = gpu::make_ortho_2d(clip.w, clip.h);
    _render_2d_batches(_d->ui_sync, proj, clip);
}

void render_gpu::_render_2d_batches(render::batcher2d::sync_id_t sync_point, const glm::mat4 &proj, render::clip_t clip)
{
    assert(clip != render::CLIP_NONE);
    auto prev_clip = render::CLIP_NONE; // always set scissor rect on first draw

    auto pass = _d->passes.current();

    // we use the material controller to get 2d rendering pipelines
    // we have to bind textures ourselves, though
    render::mat_std_2d *mat_controller = (render::mat_std_2d*)
        render::material_controller::get_controller(render::material_type::STANDARD_2D);

    // push uniforms
    SDL_PushGPUVertexUniformData(_d->cmds, 0, glm::value_ptr(proj), sizeof(glm::mat4));

    // bind buffers
    SDL_GPUBufferBinding vtx_binding = {
        .buffer = _d->buffers2d.buf_gpu_v,
        .offset = 0 // Offset where this frame's vertices start
    };
    SDL_BindGPUVertexBuffers(pass, 0, &vtx_binding, 1);
    SDL_GPUBufferBinding idx_binding = {
        .buffer = _d->buffers2d.buf_gpu_i,
        .offset = 0 // Offset where this frame's indices start
    };
    SDL_BindGPUIndexBuffer(pass, &idx_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    const auto &batcher = _d->batcher;
    auto cmd_range = batcher.sync_point_get(sync_point);
    SDL_GPUTexture *bound_texture = nullptr;
    render::sampler_config bound_sampler_config {};
    bool has_bound_sampler = false;
    for (size_t i = 0; i < cmd_range.cmd_count; i++)
    {
        const auto &cmd = batcher.commands()[cmd_range.cmd_offset + i];

        auto intersect = render::clip_intersect(clip, cmd.clip);
        if(intersect == render::CLIP_EMPTY)
            continue; // skip altogether
        if (intersect != prev_clip)
        {
            SDL_Rect scissor = { (int)intersect.x, (int)intersect.y, (int)intersect.w, (int)intersect.h };
            SDL_SetGPUScissor(pass, &scissor);
            prev_clip = intersect;
        }

        if(cmd.texture != -1)
        {
            auto &rtex = batcher.data().tex[cmd.texture];
            if(!rtex->rptr)
                continue;

            gpu::texture_block *tb = (gpu::texture_block*) rtex->rptr;
            render::sampler_config sampler {};
            const auto filter = rtex->nearest
                ? render::sampler_filter::NEAREST : render::sampler_filter::LINEAR;
            sampler.min_filter = filter;
            sampler.mag_filter = filter;
            sampler.mip_filter = render::sampler_filter::NEAREST;
            const auto addressing = render::has_flag(
                rtex->flags, render::texture_flags::SDL_SURFACE)
                ? render::sampler_addressing::REPEAT
                : render::sampler_addressing::CLAMP_TO_EDGE;
            sampler.addr_mode_u = addressing;
            sampler.addr_mode_v = addressing;
            sampler.addr_mode_w = render::sampler_addressing::CLAMP_TO_EDGE;
            sampler.max_lod = static_cast<float>(tb->num_levels - 1);
            if(!has_bound_sampler || bound_texture != tb->gtex ||
               bound_sampler_config != sampler)
            {
                auto *gpu_sampler = _d->samplers.get(_d->gdev, sampler);
                if(!gpu_sampler)
                    continue;
                SDL_GPUTextureSamplerBinding binding = {
                    .texture = tb->gtex,
                    .sampler = gpu_sampler
                };

                // slot 0 maps to set 2, binding 0 in SPIR-V
                SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
                bound_texture = tb->gtex;
                bound_sampler_config = sampler;
                has_bound_sampler = true;
            }
        }

        render::pipeline_descriptor pd {};
        mat_controller->make_pipeline_desc(*mat_controller->get_token_material(cmd),
                        nullptr, render::pass_type::FORWARD,
                        render::vertex_type::STANDARD_2D, pd);
        pd.targets = _d->passes.current_target_block()->pt_info;
        auto pipeline = _d->pipelines.find_or_create(_d->gdev, pd);

        if(!pipeline || !pipeline->pipeline)
            assert(false);

        SDL_BindGPUGraphicsPipeline(pass, pipeline->pipeline);

        SDL_DrawGPUIndexedPrimitives(pass, cmd.index_count, 1, cmd.index_start, cmd.base_vertex, 0);
    }
}



void render_gpu::_render_3d_layer(const render_layer &l, int layer_mark_index)
{
    const auto &marker = _d->collector_3d.markers()[layer_mark_index];
    const auto &passes = _d->collector_3d.passes();
    for(uint32_t pass_index = marker.pass_start;
        pass_index < marker.pass_start + marker.pass_count;
        ++pass_index)
    {
        const auto &pass = passes[pass_index];
        const auto &pass_desc = _d->collector_3d.pass_desc(pass);
        if(!_d->passes.bind_pass(pass_desc, l, _d->cmds))
            continue;
        bool pass_drawn = false;

        auto pass_viewport = pass_desc.viewport;
        if(pass_viewport == render::VIEWPORT_DEFAULT)
        {
            rtexture *target_texture = nullptr;
            uint32_t mip_level = 0;
            for(int i = 0; i < render::PASS_MAX_COLOR_TARGETS; ++i)
            {
                if(pass_desc.target.color_textures[i])
                {
                    target_texture = pass_desc.target.color_textures[i];
                    mip_level = pass_desc.targets.color_mip_level[i];
                    break;
                }
            }
            if(!target_texture && pass_desc.target.depth_texture)
            {
                target_texture = pass_desc.target.depth_texture;
                mip_level = pass_desc.targets.depth_mip_level;
            }
            if(!target_texture && pass_desc.target.uses_managed_target())
            {
                const auto *target = _d->targets.target_get(pass_desc.target.managed_target);
                if(target && target->color_texture_count)
                {
                    target_texture = target->color_textures[0].get();
                    mip_level = pass_desc.targets.color_mip_level[0];
                }
                else if(target && target->depth_texture)
                {
                    target_texture = target->depth_texture.get();
                    mip_level = pass_desc.targets.depth_mip_level;
                }
            }
            if(!target_texture)
            {
                log::warn("[render_gpu] _render_3d_layer: cannot resolve target dimensions for pass");
                _d->passes.unbind();
                continue;
            }

            auto mip_extent = [mip_level](uint32_t extent) {
                uint32_t level = mip_level;
                while(level > 0 && extent > 1)
                {
                    extent /= 2;
                    --level;
                }
                return static_cast<int>(std::max(1u, extent));
            };
            pass_viewport = {0, 0, mip_extent(target_texture->width),
                             mip_extent(target_texture->height)};
        }
        SDL_GPUViewport viewport = { static_cast<float>(pass_viewport.x),
                                     static_cast<float>(pass_viewport.y),
                                     static_cast<float>(pass_viewport.w),
                                     static_cast<float>(pass_viewport.h),
                                     0.0f, 1.0f };
        SDL_SetGPUViewport(_d->passes.current(), &viewport);

        // shadow state tracking
        static constexpr int MAX_TRACKED_SAMPLERS = 8;
        const void * bound_mesh {nullptr};
        const void * bound_model_uniform {nullptr};
        const void * bound_mat_vert_uniform {nullptr};
        const void * bound_mat_frag_uniform {nullptr};
        render::pipeline_descriptor::hash_t bound_pipeline {0};
        std::array<SDL_GPUTexture*, MAX_TRACKED_SAMPLERS> bound_textures {};
        std::array<render::sampler_config, MAX_TRACKED_SAMPLERS> bound_samplers {};

        if(pass.scene_uniforms_size)
        {
            const auto *scene_uniforms =
                _d->collector_3d.arena_ptr(pass.scene_uniforms_offset);
            SDL_PushGPUVertexUniformData(_d->cmds, 0, scene_uniforms,
                                        pass.scene_uniforms_size);
            SDL_PushGPUFragmentUniformData(_d->cmds, 0, scene_uniforms,
                                          pass.scene_uniforms_size);
        }
        const uint32_t material_fragment_uniform_slot =
            pass.scene_uniforms_size ? 1 : 0;

        const auto &all_cmds = _d->collector_3d.commands();

        for(uint32_t cmd_i = pass.cmd_start; cmd_i < pass.cmd_start + pass.cmd_count; ++cmd_i)
        {
        const auto &cmd = all_cmds[cmd_i];
        bool command_ready = true;

        // check need rebind pipeline
        auto p_hash = _d->collector_3d.pipeline_hashes()[cmd.pipeline_index];
        if(p_hash != bound_pipeline)
        {
            gpu::pipeline_object *pobj = _d->pipelines.find_or_create_prehashed(_d->gdev,
                                        _d->collector_3d.pipeline_descriptors()[cmd.pipeline_index], p_hash);
            if(!pobj || !pobj->pipeline)
            {
                log::warn("[render_gpu] _render_3d_layer: invalid pipeline: 0x%08x", p_hash);
                continue;
            }
            else
            {
                SDL_BindGPUGraphicsPipeline(_d->passes.current(), pobj->pipeline);
                bound_pipeline = p_hash;
            }
        }

        // check need rebind buffers
        const auto mesh = _d->collector_3d.staging_meshes()[cmd.mesh_index];
        if(bound_mesh != mesh)
        {
            _d->buffers.bind(_d->passes.current(), mesh);
            bound_mesh = mesh;
        }

        // check need rebind model uniforms
        const void *model_uniform_addr = cmd.model_uniforms_offset != UINT32_MAX
            ? _d->collector_3d.arena_ptr(cmd.model_uniforms_offset) : nullptr;
        if(model_uniform_addr && bound_model_uniform != model_uniform_addr)
        {
            SDL_PushGPUVertexUniformData(_d->cmds, 1, model_uniform_addr, sizeof(render::model_uniforms_std));
            bound_model_uniform = model_uniform_addr;
        }

        // check need rebind material vert uniforms
        if(cmd.mat_vert_uniforms_len)
        {
            const auto mat_vert_uniform_addr = _d->collector_3d.arena_ptr(cmd.mat_vert_uniforms_offset);
            if(bound_mat_vert_uniform != mat_vert_uniform_addr)
            {
                SDL_PushGPUVertexUniformData(_d->cmds, 2, mat_vert_uniform_addr, cmd.mat_vert_uniforms_len);
                bound_mat_vert_uniform = mat_vert_uniform_addr;
            }
        }

        // check need rebind material frag uniforms
        if(cmd.mat_frag_uniforms_len)
        {
            const auto mat_frag_uniform_addr = _d->collector_3d.arena_ptr(cmd.mat_frag_uniforms_offset);
            if(bound_mat_frag_uniform != mat_frag_uniform_addr)
            {
                SDL_PushGPUFragmentUniformData(_d->cmds, material_fragment_uniform_slot,
                                               mat_frag_uniform_addr, cmd.mat_frag_uniforms_len);
                bound_mat_frag_uniform = mat_frag_uniform_addr;
            }
        }

        // check texture binds
        for(int bind_i = 0; bind_i < cmd.tex_bind_count; ++bind_i)
        {
            const auto &bind = *((render::material_texture_binding*)_d->collector_3d.arena_ptr(cmd.tex_binds_offset) + bind_i);
            gpu::texture_block *tb = (gpu::texture_block*) bind.texture->rptr;
            if(tb)
            {
                auto sampler_config = bind.sampler;
                sampler_config.max_lod = std::min(sampler_config.max_lod,
                    static_cast<float>(tb->num_levels - 1));
                const bool track = bind.slot < MAX_TRACKED_SAMPLERS;
                if(track && bound_textures[bind.slot] == tb->gtex &&
                   bound_samplers[bind.slot] == sampler_config)
                    continue;
                auto *gpu_sampler = _d->samplers.get(_d->gdev, sampler_config);
                if(!gpu_sampler)
                {
                    command_ready = false;
                    continue;
                }
                SDL_GPUTextureSamplerBinding gbind {
                    .texture = tb->gtex,
                    .sampler = gpu_sampler
                };
                SDL_BindGPUFragmentSamplers(_d->passes.current(), bind.slot, &gbind, 1);
                if(track)
                {
                    bound_textures[bind.slot] = tb->gtex;
                    bound_samplers[bind.slot] = sampler_config;
                }
            }
            else if(!tb){
                log::warn("[render_gpu] _render_3d_layer: missing texture block for 0x%08x!", bind.texture->id());
                command_ready = false;
            }
        }

        if(!command_ready)
            continue;

        // finally, we draw
        auto ind_offset = mesh->submeshes()[cmd.submesh].index_offset;
        auto ind_count  = mesh->submeshes()[cmd.submesh].index_count;
        auto vtx_offset = mesh->submeshes()[cmd.submesh].vertex_offset;
        SDL_DrawGPUIndexedPrimitives(_d->passes.current(), ind_count, 1, ind_offset, vtx_offset, 0);
        pass_drawn = true;
        }

        _d->passes.unbind();
        rtexture *explicit_mipmap_target = nullptr;
        if(pass_desc.generate_mipmaps_after_render)
        {
            explicit_mipmap_target = pass_desc.target.color_textures[0];
            if(!explicit_mipmap_target && pass_desc.target.uses_managed_target())
            {
                const auto *target = _d->targets.target_get(pass_desc.target.managed_target);
                if(target && target->color_texture_count)
                    explicit_mipmap_target = target->color_textures[0].get();
            }
        }
        if(pass_drawn && pass_desc.generate_mipmaps_after_render)
        {
            if(!_d->textures.generate_mipmaps_for_target(_d->cmds, explicit_mipmap_target))
                log::error("[render_gpu] failed to generate requested target mipmaps");
        }

        auto generate_deferred_mipmaps = [&](rtexture *texture) {
            if(texture && texture != explicit_mipmap_target &&
               render::has_flag(texture->flags, render::texture_flags::GENERATE_MIPMAPS_DEFERRED))
                _d->textures.generate_mipmaps_deferred(_d->cmds, texture);
        };
        for(auto *texture : pass_desc.target.color_textures)
            generate_deferred_mipmaps(texture);
        generate_deferred_mipmaps(pass_desc.target.depth_texture);
        if(pass_desc.target.uses_managed_target())
        {
            const auto *target = _d->targets.target_get(pass_desc.target.managed_target);
            if(target)
            {
                for(int i = 0; i < target->color_texture_count; ++i)
                    generate_deferred_mipmaps(target->color_textures[i].get());
                generate_deferred_mipmaps(target->depth_texture.get());
            }
        }

    }
}



// RTT interface

render::target_id_t render_gpu::target_create(const render::target_desc& desc)
{
    return _d->targets.target_create(desc);
}

bool render_gpu::target_destroy(render::target_id_t id)
{
    return _d->targets.target_destroy(id);
}

std::shared_ptr<rtexture> render_gpu::target_get_color_texture(render::target_id_t id) const
{
    auto block = _d->targets.target_get(id);
    return block? (block->color_texture_count > 0? block->color_textures[0] : nullptr) : nullptr;
}

std::shared_ptr<rtexture> render_gpu::target_get_depth_texture(render::target_id_t id) const
{
    auto block = _d->targets.target_get(id);
    return block? block->depth_texture : nullptr;
}

glm::ivec2 render_gpu::target_get_size(render::target_id_t id) const
{
    auto block = _d->targets.target_get(id);
    return block? glm::vec2{ block->req_w, block->req_h } : glm::vec2{-1.f, -1.f};
}

bool render_gpu::target_has_depth(render::target_id_t id) const
{
    auto block = _d->targets.target_get(id);
    return block? block->depth_texture.get()!=nullptr : false;
}


// basic getters

int   render_gpu::window_width()  const { return _d->rwin.width(); }
int   render_gpu::window_height() const { return _d->rwin.height(); }
float render_gpu::display_scale() const { return _d->rwin.ui_scale(); }


// Picker service

entt::entity render_gpu::pick(const render_layer &layer, float vp_x, float vp_y)
{
    if (layer.three_dee)
        return _d->picker_3d.pick(layer, vp_x, vp_y);
    return _d->picker.pick(layer, vp_x, vp_y);
}
