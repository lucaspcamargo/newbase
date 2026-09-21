#include <newbase/sys/render_2d/render_2d.hpp>
#include <newbase/engine.hpp>
#include <newbase/components/sprite.hpp>
#include <newbase/components/mesh2d.hpp>
#include <newbase/components/particle_emitter.hpp>
#include <newbase/components/spatial.hpp>
#include <newbase/components/structure.hpp>
#include <newbase/components/camera.hpp>
#include <newbase/components/layers.hpp>
#include <newbase/render/window.hpp>
#include <newbase/render/collector2d.hpp>
#include <newbase/res/sprite.hpp>
#include <newbase/res/texture.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/reflection/contexts.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/ui/imgui_nb.hpp>
#include <newbase/services/ui_manager.hpp>
#include <newbase/sdl/utils.hpp>
#include <newbase/log.hpp>

#include "SDL3/SDL_rect.h"
#include "SDL3/SDL_render.h"
#include "SDL3/SDL_surface.h"
#include "SDL3/SDL_video.h"
#include "glm/fwd.hpp"
//#include "imgui_internal.h" // for ImGuiViewport
#include "newbase/layer.hpp"
#include "newbase/render/batcher2d.hpp"
#include "newbase/render/camera.hpp"
#include "newbase/render/types.hpp"
#include "newbase/services/renderer_service.hpp"
#include "newbase/system.hpp"
#include <entt/entt.hpp>
#include <newbase/utility/glm.hpp>
#include <ryml.hpp>
#include <ryml_std.hpp>
#include <tracy/Tracy.hpp>

#include <cmath>
#include <string>
#include <vector>

using namespace nb;
using entt::operator""_hs;


struct nb::render_2d_p
{
    render::window rwin;
    SDL_Renderer  *render {nullptr};
    int            wx {0}, wy {0};
    float          ui_scale {1.0};
    SDL_Rect       safe_area {};

    bool has_ui {false};
    imgui_nb imgui;

    render::batcher2d batcher;
    render::collector2d collector;

    SDL_ScaleMode default_tex_scalemode {SDL_SCALEMODE_NEAREST};

    std::shared_ptr<rtexture> smpte;
};


// destructor callback registered in rtexture instances
static void _texture_cleanup(rtexture &tex, void*);

// HACK: temporary editor grid — to be replaced with a proper grid layer/component
static void _draw_editor_grid_hack(SDL_Renderer *render,
    float cam_cx, float cam_cy, float zoom,
    int vp_x, int vp_y, int vp_w, int vp_h);

#ifdef TRACY_ENABLE
static SDL_Surface *_tracyCopy {nullptr};
#endif


render_2d::render_2d()
{
    log::info("[render_2d] constructed");
    
    _d = std::make_unique<render_2d_p>();

    // register services
    entt::locator<renderer_service*>::emplace(this);
    entt::locator<picker_service*>::emplace(this);
}

render_2d::~render_2d()
{
    log::info("[render_2d] destroying");
    if(_d->has_ui)
    {
        _d->imgui.teardown();
        ui_manager* ui_mgr = entt::locator<ui_manager*>::value();
        if(ui_mgr)
        {
            ui_mgr->ui_destroy();
        }
        else
            log::warn("[render_2d] could not locate ui service for destruction");
    }
    if(_d->render)
    {
        SDL_DestroyRenderer(_d->render);
    }

    // release our private data
    // render::window is destroyed in tandem
    _d.reset();

    log::info("[render_2d] destroyed");
}


SDL_InitFlags render_2d::sdl_subsystems(ryml::ConstNodeRef cfg)
{
    // TODO doesn't seem to work in runtime
    // No SDL hints have found for this either
    // also msvcpp does not like env manipulation on windows
    if(cfg.has_child("prefer") && !cfg["prefer"].invalid())
    {
        log::info("[render_2d] current driver: %s", SDL_GetCurrentVideoDriver());
        bool already = getenv("SDL_VIDEODRIVER");
        std::string preferred;
        cfg["prefer"] >> preferred;
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, preferred.c_str());
        log::info("[render_2d] preferring: %s%s", preferred.c_str(), already?" (overrides env)":"");
    }
    return SDL_INIT_VIDEO;
}


bool render_2d::init(ryml::ConstNodeRef cfg)
{
    log::info("[render_2d] init");
    const auto num_drivers = SDL_GetNumRenderDrivers();

    log::info("[render_2d] current driver: %s", SDL_GetCurrentVideoDriver());
    

    if (cfg.has_child("dump_backkends"))
    {
        bool dump;
        cfg["dump_backends"] >> dump;
        if (dump)
        {
            // we dont do this by default because it is slow
            std::vector<std::string> drivers;
             std::string driver_names;
             for(int i = 0; i < num_drivers; i++)
             {
             const auto driver = SDL_GetRenderDriver(i);
             drivers.push_back(driver);
             driver_names += driver;
             driver_names += " ";
        }

        log::info("[render_2d] available drivers: %s", driver_names.c_str());
        }
    }

    if(!_d->rwin.create(cfg, SDL_WINDOW_OPENGL))
    {
        log::error("[render_2d] window creation failed");
        return false;
    }

    log::info("[render_2d] window scale: %f", _d->ui_scale);

    _d->render = SDL_CreateRenderer(_d->rwin.get(), nullptr);
    log::info("[render_2d] renderer: %s", SDL_GetRendererName(_d->render));

    SDL_SetRenderVSync(_d->render, 1);
    if (_d->render == nullptr)
    {
        log::error("[render_2d] SDL_CreateRenderer(): %s\n", SDL_GetError());
        return false;
    }
    else
        log::info("[render_2d] created renderer: %s", SDL_GetRendererName(_d->render));

    _d->rwin.center();

    if(!_d->rwin.show())
    {
        log::error("[render_2d] window show() filed: %s\n", SDL_GetError());
        return false;
    }

    // get main render window attributes
    _d->wx = _d->rwin.width();
    _d->wy = _d->rwin.height();
    _d->ui_scale = _d->rwin.ui_scale();
    _d->safe_area = _d->rwin.safe_area();

    // attempt to load and set window icon
    // TODO move to render::window
    auto icon_tex = rman().get<rtexture>("_nb_core/icon_192.png"_hs);
    if(icon_tex && icon_tex->surf)
    {
        SDL_SetWindowIcon(_d->rwin.get(), icon_tex->surf);
    }

    // init gui via ui manager
    ui_manager* ui_mgr = entt::locator<ui_manager*>::value();
    if(ui_mgr)
    {
        _d->has_ui = ui_mgr->ui_init();
    }
    else
        log::warn("[render_2d] could not init ui via ui_manager service");
    
    if(_d->has_ui)
    {
        log::warn("[render_2d] ui init");
        _d->imgui.init(_d->rwin);
        ui_mgr->ui_init_finish(_d->ui_scale);
    }
    else
    {
        log::warn("[render_2d] no ui, not initializing ImGui renderer");
    }

    // load (no layers) background
    _d->smpte = rman().get<rtexture>("_nb_core/tex/smpte.png"_hs);

    return true;
}

bool render_2d::step(nb::step_phase phase)
{
    if(phase == step_phase::PREPARE)
    {
        ZoneScopedN("RenderPreUpdate");
        // Start UI frame
        ui_manager* ui_mgr = entt::locator<ui_manager*>::value();
        if(ui_mgr)
        {
            _d->imgui.new_frame();
            ui_mgr->ui_new_frame(_d->safe_area.x, _d->safe_area.y, _d->safe_area.w, _d->safe_area.h);
        }
    }
    else if(phase == step_phase::UI_RENDER)
    {
        if(_d->has_ui)
        {
            ZoneScopedN("RenderDrawUI");
            ui_manager* ui_mgr = entt::locator<ui_manager*>::value();
            ui_mgr->draw_tool_windows();
            ui_mgr->draw_perf();
        }
    }
    else if(phase == step_phase::PRE_RENDER)
    {
        ZoneScopedN("RenderPre");
        if(_d->has_ui)
        {
            ui_manager* ui_mgr = entt::locator<ui_manager*>::value();
            ui_mgr->update_viewports();
            _d->imgui.render_flush();
        }
    }
    else if(phase == step_phase::RENDER)
    {
        ZoneScopedN("Render");

        const auto &layers = engine::instance().render_layers();

        // if there isn't be anything to render
        // just draw a fun "debug" background
        if(layers.empty())
        {
            _prepare_texture(_d->smpte.get());
            auto sdltex = static_cast<SDL_Texture*>(_d->smpte->rptr);
            SDL_SetTextureScaleMode(sdltex, SDL_SCALEMODE_LINEAR);
            SDL_RenderTexture(_d->render, static_cast<SDL_Texture*>(_d->smpte->rptr), NULL, NULL);
        }

        //now render all layers
        for(const auto &layer : layers)
        {
            auto *sc = engine::instance().find_scene(layer.scene_id);
            if(!sc)
                continue;   // layer has no scene, skip

            const auto &vp = layer.viewport;

            // Clear viewport region if requested
            if(layer.clear)
            {

                //log::info("CLEAR %dx%d @ %d,%d", vp.w, vp.h, vp.x, vp.y);
                SDL_SetRenderDrawColor(_d->render,
                    static_cast<Uint8>(layer.clear_r * 255), static_cast<Uint8>(layer.clear_g * 255),
                    static_cast<Uint8>(layer.clear_b * 255), 255);
                SDL_FRect clip { static_cast<float>(vp.x), static_cast<float>(vp.y),
                                    static_cast<float>(vp.w), static_cast<float>(vp.h) };
                SDL_RenderFillRect(_d->render, &clip);
            }

            // Find camera and determine world bounds
            auto &reg = sc->registry();
            glm::vec4 world_bounds {vp.x, vp.y, vp.w, vp.h};
            if(layer.camera != entt::null)
            {
                float cam_cx = 0.0f, cam_cy = 0.0f;
                auto *sp  = reg.try_get<cspatial>(layer.camera);
                auto *cam = reg.try_get<ccamera>(layer.camera);
                if(sp)  { cam_cx = sp->pos.x; cam_cy = sp->pos.y; }
                if(cam) { world_bounds = render::camera_2d_calc_world_bounds(cam_cx, cam_cy, cam->cam2d, vp); }
                //log::info("CAM bounds %fx%f @ %f,%f MODE %d", world_bounds.z, world_bounds.w, world_bounds.x, world_bounds.y, cam?(int)cam->cam2d.fit_mode:-1);
            }

            // Build projection matrix from bounds (NDC)
            glm::mat4 proj = glm::ortho(world_bounds.x, world_bounds.x + world_bounds.z,
                                        world_bounds.y, world_bounds.y + world_bounds.w,
                                        -1.0f, 1.0f);

            // Viewport Matrix: Maps NDC [-1, 1] to Pixel Space [0, vp.w] x [0, vp.h]
            glm::mat4 view = glm::mat4(1.0f);
            view = glm::translate(view, glm::vec3(vp.x + vp.w * 0.5f, vp.y + vp.h * 0.5f, 0.0f));
            view = glm::scale(view, glm::vec3(vp.w * 0.5f, vp.h * 0.5f, 1.0f));

            // calculated view proection
            glm::mat4 viewproj = view * proj;

            SDL_Rect clip_rect { vp.x, vp.y, vp.w, vp.h };
            SDL_SetRenderClipRect(_d->render, &clip_rect);
            if (layer.use_grid)
            {
                SDL_SetRenderDrawColor(_d->render,
                    static_cast<Uint8>(layer.clear_r * 255),
                    static_cast<Uint8>(layer.clear_g * 255),
                    static_cast<Uint8>(layer.clear_b * 255), 255);
                SDL_FRect fill { (float)vp.x, (float)vp.y, (float)vp.w, (float)vp.h };
                SDL_RenderFillRect(_d->render, &fill);
                // TODO refactor this _draw_editor_grid_hack(_d->render, cam_cx, cam_cy, zoom, vp.x, vp.y, vp.w, vp.h);
            }
            _draw_scene(*sc, viewproj, layer);
            SDL_SetRenderClipRect(_d->render, nullptr);
        }
        
        // GUI
        if(_d->has_ui)
        {
            _draw_batches(_d->imgui.render_data());
        }

#ifdef TRACY_ENABLED
        // TODO
        // rebuild _tracyCopy to be able to hold current framebuffer (if needed)
        // Call SDL_RenderReadPixels() to fill buffer
        // Send buffer to tracy profiler
#endif
        SDL_RenderPresent(_d->render);
        FrameMark;
    }
    return true;
}

bool render_2d::event( SDL_Event * evt)
{
    if(_d->has_ui)
    {
        _d->imgui.event(evt);
    }

    bool window_update = _d->rwin.event(evt);
    if(window_update)
    {
        _d->wx = _d->rwin.width();
        _d->wy = _d->rwin.height();
        _d->ui_scale = _d->rwin.ui_scale();
        _d->safe_area = _d->rwin.safe_area();
    }

    // TODO move somewhere else?
    if(evt->type == SDL_EVENT_KEY_DOWN && evt->key.scancode == SDL_SCANCODE_F11)
        SDL_SetWindowFullscreen(_d->rwin.get(), !(SDL_GetWindowFlags(_d->rwin.get())&SDL_WINDOW_FULLSCREEN));

    return true;
}


void render_2d::_draw_scene(scene &scene, const glm::mat4x4 &viewproj, const render_layer &l)
{
    _d->batcher.clear();
    _d->collector.clear();

    const auto bounds = _get_viewport_bounds(l);
    auto clip = render::clip_t{bounds.first.x, bounds.first.y, bounds.second.x, bounds.second.y,};

    // use the standard 2d collector to go over scene and
    // batch geometry data
    _d->collector.collect(_d->batcher, scene, l, viewproj);

    // now go over batches and render them
    _draw_batches(_d->batcher, clip);
}


void render_2d::_draw_batches(render::batcher2d& batcher, render::clip_t clip)
{
    const auto &data = batcher.data();

    // first, ensure textures are ready
    for (auto &tex: data.tex)
    {
        _prepare_texture(tex.get());
    }

    // clipping init, handling
    render::clip_t clip_cmd_curr = render::CLIP_NONE; // used to control cmd clip changes
    if(clip != render::CLIP_NONE)
    {
        auto clip_int = render::clip_to_int(clip);
        SDL_SetRenderClipRect(_d->render, &clip_int);
    }
    else
        SDL_SetRenderClipRect(_d->render, nullptr);

    // now we draw
    for (const auto &cmd: batcher.commands())
    {
        assert(cmd.index_count);
        assert(cmd.index_start + cmd.index_count <= data.inds.size());
        assert(cmd.base_vertex < data.verts.size());

        // first, handle clip
        // command is dropped if there is clip but no intersection
        if(clip_cmd_curr != cmd.clip)
        {
            clip_cmd_curr = cmd.clip;
            render::clip_t intersect = render::clip_intersect(clip_cmd_curr, clip);
            if(intersect == render::CLIP_EMPTY)
                continue; // empty clip intersection, go to next draw command
            else if(intersect == render::CLIP_NONE)
            {
                // no clipping now, just disable it
                SDL_SetRenderClipRect(_d->render, nullptr);
            }
            else
            {
                // valid clip intersection, apply it
                SDL_Rect clip_int = render::clip_to_int(intersect);
                SDL_SetRenderClipRect(_d->render, &clip_int);
            }
        }

        const render::vertex2d *vtx0 = data.verts.data() + cmd.base_vertex;
        const uint16_t *ind0 = data.inds.data() + cmd.index_start;
        constexpr auto v_stride = sizeof(render::vertex2d);

        auto rtex = cmd.texture != -1? data.tex[cmd.texture].get() : nullptr;
        auto sdltex = rtex? static_cast<SDL_Texture*>(rtex->rptr) : nullptr;

        // if (!rtex->uploaded) continue; // we could skip rendering textures that fail to upload, or not

        // NOTE We could simplify these blend mode shenanigans by:
        //  -- always restoring draw blend mode in the end
        //  -- always setting texture blend mode before rendering, as we are the only ones who should be rendering them
        // It matters little, though, since SDL is doing it's own batching underneath anyway...

        SDL_BlendMode blend_prev {};
        SDL_BlendMode blend_now = static_cast<SDL_BlendMode>(cmd.blend);
        if(sdltex)
        {
            SDL_GetTextureBlendMode(sdltex, &blend_prev);
            SDL_SetTextureBlendMode(sdltex, blend_now);
        }
        else
        {
            SDL_GetRenderDrawBlendMode(_d->render, &blend_prev);
            SDL_SetRenderDrawBlendMode(_d->render, blend_now);
        }

        // OUR DRAW CALL
        SDL_RenderGeometryRaw(_d->render, sdltex,
                              static_cast<const float*>(&(vtx0->pos.x)), v_stride,
                              reinterpret_cast<const SDL_FColor*>(&(vtx0->color.x)), v_stride,
                              static_cast<const float*>(&(vtx0->uv.x)), v_stride,
                              cmd.vtx_count, ind0, cmd.index_count, 2
        );

        // restore previous blend mode, whatever that was
        if(sdltex)
        {
            SDL_SetTextureBlendMode(sdltex, blend_prev);
        }
        else
        {
            SDL_SetRenderDrawBlendMode(_d->render, blend_prev);
        }
    }

    SDL_SetRenderDrawBlendMode(_d->render, SDL_BLENDMODE_BLEND);
    SDL_SetRenderClipRect(_d->render, nullptr);
}

void render_2d::_prepare_texture(rtexture *rtex)
{
    assert(rtex);  // this shuldd never happen

    auto sdltex = static_cast<SDL_Texture*>(rtex->rptr);

    // if we need to do an upload
    if (!rtex->uploaded && rtex->surf)
    {
        if(sdltex)
        {
            // destroy existing texture if needed
            SDL_DestroyTexture(sdltex);
            sdltex = nullptr;
            rtex->rptr = nullptr;
        }

        // Always create a new texture
        rtex->rptr = sdltex = SDL_CreateTextureFromSurface(_d->render, rtex->surf);
        log::warn("[render_2d] texture created for 0x%08x: %dx%d", rtex->id(), rtex->width, rtex->height);
        // ...and register its cleanup on resource deletion
        rtex->on_delete = &_texture_cleanup;
        rtex->on_delete_uptr = _d.get();

        if(sdltex)
        {
            SDL_SetTextureScaleMode(sdltex, _d->default_tex_scalemode);
            rtex->uploaded = true;
        }
        else
            log::warn("[render_2d] texture upload failure for 0x%08x", rtex->id());

        // even on upload failure, we destroy the surface, to prevent continuous failure every frame
        SDL_DestroySurface(rtex->surf);
        rtex->surf = nullptr;
    }
}


std::pair<glm::vec2, glm::vec2> render_2d::_get_viewport_bounds(const render_layer &l)
{
    const auto &vp = l.viewport;
    const glm::vec2 pos = {vp.x, vp.y};
    const glm::vec2 dims = {vp.w, vp.h};

   return std::make_pair(pos, dims);
}


// TODO move to nb::render namespace, generalize, drop service
// TODO generalize camera behavior and math in a single place and use that,
//      also for rendering
entt::entity render_2d::pick(const render_layer &layer, float vp_x, float vp_y)
{
    return entt::null;  // disabled for now
    /*
    auto *sc = engine::instance().find_scene(layer.scene_id);
    if (!sc) { log::warn("[pick] no scene"); return entt::null; }

    const auto &vp = layer.viewport;

    // Viewport-local → window → world
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

    log::info("[pick] vp(%.0f,%.0f) win(%.0f,%.0f) world(%.1f,%.1f) cam(%.1f,%.1f) zoom=%.2f vp_rect=%d,%d %dx%d",
        vp_x, vp_y, win_x, win_y, wx, wy, cam_cx, cam_cy, zoom, vp.x, vp.y, vp.w, vp.h);

    entt::entity best  = entt::null;
    float        best_z = std::numeric_limits<float>::max();

    for (auto [id, spatial] : reg.view<const cspatial>().each())
    {
        // Layer mask check
        const auto *lyr_comp = reg.try_get<clayers>(id);
        const uint32_t entity_mask = lyr_comp ? lyr_comp->mask : clayers::MASK_DEFAULT;
        if (!(entity_mask & layer.layer_mask)) continue;

        if (auto *sprite = reg.try_get<const csprite>(id))
        {
            if (!sprite->visible || !sprite->spr) continue;
            auto &spr = *sprite->spr;

            glm::vec2 dims = spr.dims;
            if (dims == glm::vec2{-1.f, -1.f})
            {
                const glm::vec4 &csr = sprite->current_source_rect;
                if (csr.z > 0.f)
                    dims = { csr.z, csr.w };
                else if (spr.tex && spr.tex->uploaded && spr.tex->rptr)
                    dims = { (float)static_cast<SDL_Texture*>(spr.tex->rptr)->w, (float)static_cast<SDL_Texture*>(spr.tex->rptr)->h };
                else continue;
            }

            // Transform pick point into local space and test against sprite quad
            const glm::vec4 local = glm::inverse(spatial.world) * glm::vec4{wx, wy, 0.f, 1.f};
            const float ql = -spr.anchor.x * dims.x;
            const float qt = -spr.anchor.y * dims.y;
            const bool hit = local.x >= ql && local.x <= ql + dims.x &&
                             local.y >= qt && local.y <= qt + dims.y;
            log::info("[pick]   sprite eid=%x pos=(%.1f,%.1f,%.1f) dims=(%.0fx%.0f) local=(%.1f,%.1f) quad=[%.1f..%.1f, %.1f..%.1f] hit=%d",
                entt::to_integral(id), spatial.pos.x, spatial.pos.y, spatial.pos.z,
                dims.x, dims.y, local.x, local.y, ql, ql+dims.x, qt, qt+dims.y, hit);
            if (hit && spatial.pos.z < best_z) { best_z = spatial.pos.z; best = id; }
        }
        else if (auto *mesh = reg.try_get<const cmesh2d>(id))
        {
            if (!mesh->visible || !mesh->geom || mesh->geom->empty()) continue;

            const glm::vec4 local4 = glm::inverse(spatial.world) * glm::vec4{wx, wy, 0.f, 1.f};
            const glm::vec2 lp { local4.x, local4.y };
            const auto &geom  = *mesh->geom;
            const auto &verts = geom.vertices;

            // Sign of cross product for point-in-triangle test
            auto tri_hit = [&](int i0, int i1, int i2) {
                const glm::vec2 a{verts[i0].pos}, b{verts[i1].pos}, c{verts[i2].pos};
                const float d1 = (lp.x-b.x)*(a.y-b.y) - (a.x-b.x)*(lp.y-b.y);
                const float d2 = (lp.x-c.x)*(b.y-c.y) - (b.x-c.x)*(lp.y-c.y);
                const float d3 = (lp.x-a.x)*(c.y-a.y) - (c.x-a.x)*(lp.y-a.y);
                return !((d1<0||d2<0||d3<0) && (d1>0||d2>0||d3>0));
            };

            bool hit = false;
            if (!geom.indices.empty())
            {
                for (size_t i = 0; i+2 < geom.indices.size() && !hit; i += 3)
                    hit = tri_hit(geom.indices[i], geom.indices[i+1], geom.indices[i+2]);
            }
            else
            {
                for (size_t i = 0; i+2 < verts.size() && !hit; i += 3)
                    hit = tri_hit((int)i, (int)i+1, (int)i+2);
            }
            log::info("[pick]   mesh  eid=%x verts=%zu hit=%d", entt::to_integral(id), verts.size(), hit);
            if (hit && spatial.pos.z < best_z) { best_z = spatial.pos.z; best = id; }
        }
        else if (auto *emit = reg.try_get<const cparticle_emitter>(id))
        {
            // Pick if within emission area radius (pos_variance magnitude, minimum 24px)
            float radius = 24.f;
            if (emit->res)
            {
                const glm::vec2 &pv = emit->res->emitter.pos_variance;
                radius = std::max(24.f, glm::length(pv));
            }
            const float dx = wx - spatial.pos.x, dy = wy - spatial.pos.y;
            const bool hit = dx*dx + dy*dy <= radius*radius;
            log::info("[pick]   emit  eid=%x pos=(%.1f,%.1f) radius=%.1f hit=%d",
                entt::to_integral(id), spatial.pos.x, spatial.pos.y, radius, hit);
            if (hit && spatial.pos.z < best_z) { best_z = spatial.pos.z; best = id; }
        }
        else
        {
            // No visual component: small fixed-radius hit area
            constexpr float HIT_RADIUS = 8.f;
            const float dx = wx - spatial.pos.x, dy = wy - spatial.pos.y;
            const bool hit = dx*dx + dy*dy <= HIT_RADIUS*HIT_RADIUS;
            log::info("[pick]   bare  eid=%x pos=(%.1f,%.1f,%.1f) dist=%.1f hit=%d",
                entt::to_integral(id), spatial.pos.x, spatial.pos.y, spatial.pos.z,
                sqrtf(dx*dx+dy*dy), hit);
            if (hit && spatial.pos.z < best_z) { best_z = spatial.pos.z; best = id; }
        }
    }

    log::info("[pick] result: %s (eid=%x)", best == entt::null ? "null" : "hit", entt::to_integral(best));
    return best;*/
}

void render_2d::cam_2d_setup(float cx, float cy, float wmax, float hmax)
{
    // TODO remove this dummy
}

float render_2d::cam_2d_scale()
{
    return 1.0f; // TODO remove this dummy
}

// TODO we will get rid of this HACK mess in a bit
//      again, taking a camera and a viewport should give out a world rect
bool render_2d::get_2d_extents(renderer_service::extents_2d &extents)
{
    const auto &layers = engine::instance().render_layers();
    if(!layers.empty())
    {
        const auto &layer = layers.front();
        auto *sc = engine::instance().find_scene(layer.scene_id);
        if(sc)
        {
            auto &reg = sc->registry();
            const auto &vp  = layer.viewport;
            float cx = 0.f, cy = 0.f, zoom = 1.f;
            if(layer.camera != entt::null)
            {
                if(auto *sp  = reg.try_get<cspatial>(layer.camera)) { cx = sp->pos.x; cy = sp->pos.y; }
                if(auto *cam = reg.try_get<ccamera>(layer.camera))  { zoom = cam->cam2d.scale; }
            }
            float span_x = vp.w / zoom;
            float span_y = vp.h / zoom;

            // on android, the ui style and font are scaled
            // but the internal imgui scale remains at 1.0
            // this should never have happened
#ifdef ANDROID
            static constexpr float ui_scale = 1.0f;
#else
            float ui_scale = _d->ui_scale;
#endif
            extents = { vp.w, vp.h, span_x, span_y,
                cx - span_x * 0.5f, cy - span_y * 0.5f,
                cx + span_x * 0.5f, cy + span_y * 0.5f,
                ui_scale, vp.x, vp.y };
            return true;
        }
    }

    return false;  // no layer, no extents
}


renderer_service::texture_handle render_2d::create_texture(int w, int h)
{
    auto ret = SDL_CreateTexture(_d->render, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, w, h);
    SDL_SetTextureScaleMode(ret, _d->default_tex_scalemode);
    return ret;
}

void render_2d::update_texture(texture_handle tex, const void* pixels, int pitch)
{
    assert(tex);
    SDL_UpdateTexture(static_cast<SDL_Texture*>(tex), nullptr, pixels, pitch);
}

void render_2d::destroy_texture(texture_handle tex)
{
    SDL_DestroyTexture(static_cast<SDL_Texture*>(tex));
}


int   render_2d::window_width()  const { return _d->wx; }
int   render_2d::window_height() const { return _d->wy; }
float render_2d::display_scale() const { return _d->ui_scale; }


void _texture_cleanup(rtexture &tex, void*)
{
    if(tex.rptr)
    {
        SDL_DestroyTexture(static_cast<SDL_Texture*>(tex.rptr));
        tex.rptr = nullptr; // for correctness
    }
}


static void _draw_editor_grid_hack(SDL_Renderer *render,
                                   float cam_cx, float cam_cy, float zoom,
                                   int vp_x, int vp_y, int vp_w, int vp_h)
{
    // Grid levels in world units. Each is 16x the previous.
    constexpr float STEPS[]       = { 1.f, 16.f, 64.f, 256.f, 1024.f };
    constexpr Uint8 MAX_ALPHA[]   = { 35,   45,   60,    75,    90   };
    constexpr int   NUM_LEVELS    = 5;
    constexpr float FADE_IN_MIN   = 4.f;   // screen px below which lines disappear
    constexpr float FADE_IN_FULL  = 24.f;  // screen px at which lines reach full alpha

    const float vp_cx = vp_x + vp_w * 0.5f;
    const float vp_cy = vp_y + vp_h * 0.5f;
    const float wl = (vp_x        - vp_cx) / zoom + cam_cx;
    const float wr = (vp_x + vp_w - vp_cx) / zoom + cam_cx;
    const float wt = (vp_y        - vp_cy) / zoom + cam_cy;
    const float wb = (vp_y + vp_h - vp_cy) / zoom + cam_cy;

    SDL_SetRenderDrawBlendMode(render, SDL_BLENDMODE_BLEND);

    for (int li = 0; li < NUM_LEVELS; ++li)
    {
        const float step      = STEPS[li];
        const float screen_px = step * zoom;
        if (screen_px < FADE_IN_MIN) continue;

        const float t     = std::min(1.f, (screen_px - FADE_IN_MIN) / (FADE_IN_FULL - FADE_IN_MIN));
        const Uint8 alpha = static_cast<Uint8>(t * MAX_ALPHA[li]);
        if (alpha < 2) continue;

        SDL_SetRenderDrawColor(render, 180, 180, 200, alpha);

        const float x0 = floorf(wl / step) * step;
        for (float wx = x0; wx <= wr; wx += step)
        {
            float sx = (wx - cam_cx) * zoom + vp_cx;
            SDL_RenderLine(render, sx, (float)vp_y, sx, (float)(vp_y + vp_h));
        }

        const float y0 = floorf(wt / step) * step;
        for (float wy = y0; wy <= wb; wy += step)
        {
            float sy = (wy - cam_cy) * zoom + vp_cy;
            SDL_RenderLine(render, (float)vp_x, sy, (float)(vp_x + vp_w), sy);
        }
    }

    // World-space axes — always visible, higher alpha
    SDL_SetRenderDrawColor(render, 180, 180, 230, 140);
    const float ox = (0.f - cam_cx) * zoom + vp_cx;
    const float oy = (0.f - cam_cy) * zoom + vp_cy;
    if (ox >= vp_x && ox <= vp_x + vp_w)
        SDL_RenderLine(render, ox, (float)vp_y, ox, (float)(vp_y + vp_h));
    if (oy >= vp_y && oy <= vp_y + vp_h)
        SDL_RenderLine(render, (float)vp_x, oy, (float)(vp_x + vp_w), oy);

    SDL_SetRenderDrawBlendMode(render, SDL_BLENDMODE_NONE);
}

// RTTI metadata
extern "C" void _rtti_init_render_2d()
{
    entt::meta_factory<nb::render_2d>{}
        .type("render_2d"_hs)
        .custom<rtti::type_info>(rtti::type_info{"render_2d", rtti::TYPE_CLASS_SYSTEM})
        .base<nb::system>()
        .func<&nb::render_2d::cam_2d_setup>("cam_2d_setup"_hs)
        .custom<rtti::func_info>(rtti::func_info{"cam_2d_setup"})
        .func<&nb::render_2d::cam_2d_scale>("cam_2d_scale"_hs)
        .custom<rtti::func_info>(rtti::func_info{"cam_2d_scale"})
        .func<&nb::render_2d::window_width>("window_width"_hs)
        .custom<rtti::func_info>(rtti::func_info{"window_width"})
        .func<&nb::render_2d::window_height>("window_height"_hs)
        .custom<rtti::func_info>(rtti::func_info{"window_height"})
        .func<&nb::render_2d::display_scale>("display_scale"_hs)
        .custom<rtti::func_info>(rtti::func_info{"display_scale"});
    entt::meta_factory<std::shared_ptr<nb::render_2d>>{rtti::ctx_systems()}
        .type("render_2d_shared"_hs)
        .ctor<&rtti::shared_ptr_builder<nb::render_2d>>()
        .conv<std::shared_ptr<nb::system>>();

    cspatial::_ensure_rtti();
    cstructure::_ensure_rtti();
    csprite::_ensure_rtti();
    cmesh2d::_ensure_rtti();
    cparticle_emitter::_ensure_rtti();
    ccamera::_ensure_rtti();
    clayers::_ensure_rtti();
}
