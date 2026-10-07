#include "newbase/render/material.hpp"
#include "newbase/render/vertex.hpp"
#include <newbase/sys/render_gpu/render_gpu.hpp>
#include <newbase/sys/render_gpu/buffers.hpp>
#include <newbase/sys/render_gpu/pipelines.hpp>
#include <newbase/sys/render_gpu/pass_control.hpp>
#include <newbase/sys/render_gpu/shaders.hpp>
#include <newbase/sys/render_gpu/targets.hpp>
#include <newbase/sys/render_gpu/textures.hpp>
#include <newbase/sys/render_gpu/util2d.hpp>
#include <newbase/engine.hpp>
#include <newbase/scene.hpp>
#include <newbase/layer.hpp>
#include <newbase/geom/picker2d.hpp>
#include <newbase/render/batcher2d.hpp>
#include <newbase/render/collector2d.hpp>
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

// TEST
#include <newbase/sys/render_gpu/test_cube.hpp>
#include <newbase/res/shader.hpp>
#include <newbase/components/mesh.hpp>
#include <newbase/components/camera.hpp>
#include <newbase/components/spatial.hpp>

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
    gpu::pipeline_cache pipelines;

    // GPU Resource Managers
    gpu::buffer_manager buffers {};
    gpu::texture_manager textures {};
    gpu::shader_manager shaders {};
    gpu::target_manager targets {textures};
    gpu::pass_control passes {targets};

    // 2D rendering data
    render::batcher2d batcher;
    render::collector2d collector;
    render::collector2d::results collector_results;
    gpu::shaders2d shaders2d;
    gpu::buffers2d buffers2d;

    // UI integration
    bool has_ui {false};
    imgui_nb imgui;
    render::batcher2d::sync_id_t ui_sync;

    // picking
    geom::picker_2d picker;

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

    if(!_d->shaders2d.load(_d->gdev))
    {
        log::warn("[render_gpu] failed to load basic 2d shaders");
    }
    else if(!_d->shaders2d.prepare([&](rshader*s)->bool{return _d->shaders.prepare(_d->gdev, s);}))
    {
        log::warn("[render_gpu] failed to prepare basic 2d shaders");
    }

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
    _d->collector.clear();
    _d->shaders2d.clear();
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
        // free resources marked for deletion
        _d->textures.cleanup(_d->gdev);
        _d->shaders.cleanup(_d->gdev, _d->pipelines);
        _d->buffers.cleanup(_d->gdev);

        // clear 2d batcher aand collector for upcoming render ops
        _d->batcher.clear();
        _d->collector.clear();

        // do another render target resize pass
        // we could have new targets and viewport added during the update cycle
        _d->targets.update_sizes(_d->ui_vp);

        // batch 2D geometry for render layer
        // opportunistically, we save the id of the empty sync point
        // for ui rendering
        _d->ui_sync = _d->collector.collect_all_layers(_d->batcher, _d->collector_results, render::collector2d::clear_quad_mode::CLEAR_QUAD_ALWAYS);
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

        // TODO 3d render command collection
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

    // TEST - creates own commandd buffer and all
    static bool cube_up = false;
    if(!cube_up)
    {
        _d->m_cube_v = UploadStaticGPUBuffer(
            _d->gdev,
            SDL_GPU_BUFFERUSAGE_VERTEX,
            CUBE_VERTICES,
            sizeof(CUBE_VERTICES)
        );
        _d->m_cube_i = UploadStaticGPUBuffer(
            _d->gdev,
            SDL_GPU_BUFFERUSAGE_INDEX,
            CUBE_INDICES,
            sizeof(CUBE_INDICES)
        );
        _d->m_cube_vert = rman().load_sync<rshader>("_nb_core/slang/cube.vert.slang"_hs);
        _d->m_cube_frag = rman().load_sync<rshader>("_nb_core/slang/cube.frag.slang"_hs);
        _d->shaders.prepare(_d->gdev, _d->m_cube_vert.get());
        _d->shaders.prepare(_d->gdev, _d->m_cube_frag.get());
        cube_up = true;
    }

    // acquire command buffer
    assert(!(_d->cmds) && "[render_gpu] leftover command buffer!");
    _d->cmds = SDL_AcquireGPUCommandBuffer(device);
    if (!_d->cmds) {
        SDL_Log("Failed to acquire command buffer: %s", SDL_GetError());
        return;
    }

    // copy necessary data
    // upload batched 2d geometry and textures
    {
        gpu::copy_scope cpy {_d->cmds};
        _d->buffers2d.upload(_d->gdev, _d->cmds, _d->batcher, cpy);
        _d->textures.prepare_multiple(_d->gdev, _d->cmds, _d->batcher.data().tex.begin(), _d->batcher.data().tex.end(), cpy);

        // TEST ensure buffer upload of all meshes in scene
        //      also prepare material shaders and textures
        entt::registry &reg = engine::instance().default_scene().registry();
        auto mesh_view = reg.view<const cmesh>();
        for (auto [id, mesh] : mesh_view.each())
        {
            if(mesh.mesh)
            {
                _d->buffers.prepare(_d->gdev, _d->cmds, mesh.mesh.get(), cpy);
            }
            auto &rmesh = *mesh.mesh.get();
            for(int sub_i = 0; sub_i < rmesh.submeshes().size(); ++sub_i)
            {
                auto &subm = rmesh.submeshes()[sub_i];
                auto &mat_ptr = mesh.materials[subm.material_idx];
                if(!mat_ptr)
                {
                    log::warn("[render_gpu] scene prepare: NO MATERIAL!");
                    continue;
                }

                auto &mat = *mat_ptr.get();

                if(mat.dirty)
                    mat.materialize();

                auto mat_ctrl = mat.controller();
                auto sh_vert = mat_ctrl->get_vertex_program(mat);
                auto sh_frag = mat_ctrl->get_fragment_program(mat);

                if(!sh_vert || !sh_frag)
                {
                    log::warn("[render_gpu] scene prepare: NO SHADERS!");
                    continue;
                }
                _d->shaders.prepare(_d->gdev, sh_vert.get());
                _d->shaders.prepare(_d->gdev, sh_frag.get());

                render::material_controller::tex_bind_vec_t tex_binds {};
                mat_ctrl->collect_texture_bindings(mat, tex_binds);
                for (const auto &bind : tex_binds)
                {
                    _d->textures.prepare(_d->gdev, _d->cmds, bind.texture.get(), cpy);
                }
            }
        }
    }

    // acquire the swapchain texture for this frame
    SDL_GPUTexture *swapchain = NULL;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(_d->cmds, window, &swapchain, NULL, NULL)) {
        log::warn("[render_gpu] failed to acquire swapchain texture: %s", SDL_GetError());
        return;
    }

    if (swapchain == NULL)
        return; // window must be hidden

    auto swapchain_format = SDL_GetGPUSwapchainTextureFormat(_d->gdev, window);

    // update target textures
    _d->targets.update_default(swapchain, swapchain_format, _d->rwin.width(), _d->rwin.height());
    _d->targets.commit_textures(_d->gdev);

    auto proj_2d = gpu::make_ortho_2d(_d->rwin.width(), _d->rwin.height());

    // go over layers and submit draw commands
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
            _render_3d_layer(layer);

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
            SDL_GPUTextureSamplerBinding binding = {
                .texture = tb->gtex,
                .sampler = tb->gsamp
            };

            // slot 0 maps to set 2, binding 0 in SPIR-V
            SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
        }

        gpu::pipeline_desc pd {};
        gpu::command2d_get_pipeline(cmd, pd, _d->shaders2d);
        _d->passes.write_target_block(pd);
        auto pipeline = _d->pipelines.find_or_create(_d->gdev, pd);

        if(!pipeline->pipeline)
            assert(false);

        SDL_BindGPUGraphicsPipeline(pass, pipeline->pipeline);

        SDL_DrawGPUIndexedPrimitives(pass, cmd.index_count, 1, cmd.index_start, cmd.base_vertex, 0);
    }
}



void render_gpu::_render_3d_layer(const render_layer &l)
{

    static float rot = .0f;
    rot += 1.f;

    SDL_GPUViewport viewport = { (float)l.viewport.x, (float)l.viewport.y,
                                 (float)l.viewport.w, (float)l.viewport.h,
                                 0.0f, 1.0f };
    float aspect = l.viewport.w/(float)l.viewport.h;

    SDL_SetGPUViewport(_d->passes.current(), &viewport);


    auto scn = engine::instance().find_scene(l.scene_id);
    if(!scn)
    {
        log::warn("[render_gpu] _render_3d_layer: no scene: %u", (unsigned)l.scene_id);
        return;
    }

    render::scene_uniform_std u_scn;

    auto cam_eid = l.camera;
    auto cam = scn->registry().try_get<ccamera>(cam_eid);
    auto cam_sp = scn->registry().try_get<cspatial>(cam_eid);
    if(false)//cam && cam_sp)
    {
        auto proj = glm::perspective(cam->cam3d.fov, aspect,
                                     cam->cam3d.clip_near, cam->cam3d.clip_far);
        auto view = glm::inverse(cam_sp->world);
        u_scn.view = view;
        u_scn.proj = proj;
        u_scn.view_proj = proj*view;
        u_scn.inv_view = glm::inverse(view);
        u_scn.camera_pos = cam_sp->pos;
    }
    else
    {
        // fallback
        u_scn.camera_pos = {-0.5f,0.5f,3.5f};
        glm::vec3 cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f); // Origin
        glm::vec3 upVector     = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::mat4 view = glm::lookAt(u_scn.camera_pos, cameraTarget, upVector);
        float fovYRadians = glm::radians(45.0f);
        float nearPlane   = 0.1f;
        float farPlane    = 50.0f;
        glm::mat4 proj = glm::perspective(fovYRadians, aspect, nearPlane, farPlane);
        u_scn.view = view;
        u_scn.proj = proj;
        u_scn.view_proj = proj*view;
        u_scn.inv_view = glm::inverse(view);
    }
    u_scn.light_color = glm::vec3{1.8f, 1.8f, 1.5f};        // TODO
    u_scn.light_dir = glm::vec3{0.0f, 1.0f, 0.5f}; // TODO

    SDL_PushGPUVertexUniformData( _d->cmds, 0, &u_scn, sizeof(u_scn));
    SDL_PushGPUFragmentUniformData( _d->cmds, 0, &u_scn, sizeof(u_scn));

    // iterate over scene mesh components
    auto mesh_view = scn->registry().view<cmesh, cspatial>();
    render::material_controller::tex_bind_vec_t tex_binds;
    for(const auto &[eid, msh, spa]: mesh_view.each())
    {
        auto &mesh = *msh.mesh.get();
        if(!mesh.submeshes().size() || !mesh.uploaded)
        {
            log::warn("MESH PROBLEMS");
            continue;
        }

        // bind mesh buffer
        _d->buffers.bind(_d->passes.current(), &mesh);

        render::model_uniforms_std u_model;
        u_model.model = spa.world;
        u_model.normal_matrix = glm::transpose(glm::inverse(u_model.model));
        SDL_PushGPUVertexUniformData(_d->cmds, 1, &u_model, sizeof(u_model));

        for(int sub_i = 0; sub_i < mesh.submeshes().size(); ++sub_i)
        {
            auto &subm = mesh.submeshes()[sub_i];
            auto &mat_ptr = msh.materials[subm.material_idx];
            if(!mat_ptr)
                continue; // no material

            auto &mat = *mat_ptr.get();

            auto mat_ctrl = mat.controller();
            auto sh_vert = mat_ctrl->get_vertex_program(mat);
            auto sh_frag = mat_ctrl->get_fragment_program(mat);

            auto vt = mesh.vertex_type();
            const auto &vt_desc = render::vertex_type_descriptor::descriptor_table().at(vt);
            auto pd = gpu::pipeline_desc {};
            pd.vert = (SDL_GPUShader*) sh_vert->rptr;
            pd.frag = (SDL_GPUShader*) sh_frag->rptr;
            pd.setup_vertex_input(vt_desc, sh_vert->meta);
            _d->passes.write_target_block(pd);
            pd.setup_material_pipeline_props(mat.pipeline_params);
            auto pipeline = _d->pipelines.find_or_create(_d->gdev, pd);
            if(!pipeline->valid())
                log::warn("PIPELINE NOT VALID OMG");
            SDL_BindGPUGraphicsPipeline(_d->passes.current(), pipeline->pipeline);

            SDL_PushGPUFragmentUniformData(_d->cmds, 0u, &u_scn, sizeof(u_scn));
            SDL_PushGPUFragmentUniformData(_d->cmds, 1u, mat.frag_uniforms.data(), mat.frag_uniforms.size());

            mat_ctrl->collect_texture_bindings(mat, tex_binds);
            for (const auto &bind : tex_binds)
            {
                gpu::texture_block *tb = (gpu::texture_block*) bind.texture->rptr;
                SDL_GPUTextureSamplerBinding gbind {
                    .texture = tb->gtex,
                    .sampler = tb->gsamp
                };
                SDL_BindGPUFragmentSamplers(_d->passes.current(), bind.slot, &gbind, 1);
            }

            SDL_DrawGPUIndexedPrimitives(_d->passes.current(), subm.index_count, 1, subm.index_offset, subm.vertex_offset, 0);

        }
    }


    return;

    /// TEST CUBE HERE

    TransformData transforms = {};

    UpdateCubeTransforms(transforms, glm::vec3{3.f, 4.f, 5.f}, (float)l.viewport.w, (float)l.viewport.h, rot);

    struct LightData {
        glm::vec4 dirLightVector {0.2857f, 0.8571f, 0.4286f, 0.0f}; // xyz = direction towards light, w = unused
        glm::vec4 dirLightColor {0.8f, 0.8f, 0.7f, 1.0f};  // rgb = color * intensity, a = unused
        glm::vec4 ambientColor {0.15f, 0.1f, 0.05f, 1.0f};   // rgb = base ambient light
    } lights = {
    };

    // NOTE it is ok for this to be sucky mess
    //      we will be implementing abstracted batching and commands later
    auto pass = _d->passes.current();

    auto desc = create_cube_pipeline_desc((SDL_GPUShader*)_d->m_cube_vert->rptr,
                                          (SDL_GPUShader*)_d->m_cube_frag->rptr,
                                          _d->m_cube_vert->meta);
    _d->passes.write_target_block(desc);
    auto pipeline = _d->pipelines.find_or_create(_d->gdev, desc);
    if(!pipeline)
    {
        log::error("INVALID DESCRIPTOR");
        return;
    }

    SDL_BindGPUGraphicsPipeline(pass, pipeline->pipeline);

    SDL_GPUBufferBinding vbo_binding = { .buffer = _d->m_cube_v, .offset = 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vbo_binding, 1);

    SDL_GPUBufferBinding ibo_binding = { .buffer = _d->m_cube_i, .offset = 0 };
    SDL_BindGPUIndexBuffer(pass, &ibo_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    // C. Push Uniform Data
    // Vertex Uniforms (Set 1 / space1, Slot 0)
    SDL_PushGPUVertexUniformData(_d->cmds, 0, &transforms, sizeof(TransformData));

    // Fragment Uniforms (Set 3 / space3, Slot 0)
    SDL_PushGPUFragmentUniformData(_d->cmds, 0, &lights, sizeof(LightData));

    // D. Execute Indexed Draw Call
    // Arguments: pass, index_count, instance_count, first_index, vertex_offset, first_instance
    SDL_DrawGPUIndexedPrimitives(pass, 36, 1, 0, 0, 0);
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
    // TODO 3D
    // just forward to default 2D cpu picker
    return _d->picker.pick(layer, vp_x, vp_y);
}

