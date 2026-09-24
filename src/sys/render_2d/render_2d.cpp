#include <newbase/sys/render_2d/render_2d.hpp>
#include <newbase/engine.hpp>
#include <newbase/components/spatial.hpp>
#include <newbase/components/camera.hpp>

#include <newbase/log.hpp>
#include <newbase/geom/picker2d.hpp>
#include <newbase/reflection/contexts.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/render/window.hpp>
#include <newbase/render/collector2d.hpp>
#include <newbase/render/batcher2d.hpp>
#include <newbase/render/camera.hpp>
#include <newbase/render/types.hpp>
#include <newbase/res/sprite.hpp>
#include <newbase/res/texture.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/services/ui_manager.hpp>
#include <newbase/sdl/utils.hpp>
#include <newbase/ui/imgui_nb.hpp>
#include <newbase/utility/glm.hpp>
#include <newbase/utility/topological_sort.hpp>

#include "./rtt_private.hpp"
#include "SDL3/SDL_blendmode.h"
#include "entt/graph/adjacency_matrix.hpp"
#include "entt/graph/fwd.hpp"

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <glm/fwd.hpp>
#include <entt/entt.hpp>
#include <entt/core/fwd.hpp>
#include <entt/graph/flow.hpp>
#include <ryml.hpp>
#include <ryml_std.hpp>
#include <tracy/Tracy.hpp>

#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>


using namespace nb;
using entt::operator""_hs;


struct nb::render_2d_p
{
    // window and some data we want to keep handy
    render::window rwin;
    int            wx {0}, wy {0};
    float          ui_scale {1.0};
    glm::ivec4     ui_vp {0};
    SDL_Rect       safe_area {};

    // renderer and some properties we want to keep at hand
    SDL_Renderer  *render {nullptr};
    SDL_PropertiesID r_props;
    int r_prop_tex_max_sz {0};

    bool has_ui {false};
    imgui_nb imgui;

    render::batcher2d batcher;
    render::collector2d collector;

    std::unordered_map<render::target_id_t, render_2d_target> targets;
    util::topological_sorter rt_resize_sorter;
    std::vector<render::target_id_t> rt_resize_order;
    bool rt_resize_order_dirty {false};  // new rts were added or removed, reorder
    bool rt_sizes_dirty {true}; // sizes have changed, even if order remains
    render::target_id_t rt_next_id {1};

    geom::picker_2d picker;

    SDL_ScaleMode default_tex_scalemode {SDL_SCALEMODE_LINEAR};

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
    

    if (cfg.has_child("dump_backends"))
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
    {
        log::info("[render_2d] created renderer: %s", SDL_GetRendererName(_d->render));
        _d->r_props = SDL_GetRendererProperties(_d->render);
        _d->r_prop_tex_max_sz = (int)SDL_GetNumberProperty(_d->r_props,
                                                           SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0);
        log::info("[render_2d] max texture size: %dx%d", _d->r_prop_tex_max_sz, _d->r_prop_tex_max_sz);
    }

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
    else if(phase == step_phase::PRE_UPDATE)
    {
        ZoneScopedN("PreUpdate");
        if(_d->has_ui)
        {
            ui_manager* ui_mgr = entt::locator<ui_manager*>::value();
            const auto prev_ui_vp = _d->ui_vp;
            _d->ui_vp = ui_mgr->update_viewports();

            // if ui viewport changed, we may need to update render target sizes
            if(prev_ui_vp != _d->ui_vp)
                _d->rt_sizes_dirty = true;
        }

        // update render target sizes if needed, before general updates
        if(_d->rt_sizes_dirty)
            _targets_resize();
    }
    else if(phase == step_phase::PRE_RENDER)
    {
        if(_d->has_ui)
        {
            _d->imgui.render_flush();
        }
    }
    else if(phase == step_phase::RENDER)
    {
        ZoneScopedN("Render");

        auto &layers = engine::instance().render_layers();

        // if there isn't be anything to render
        // just draw a fun "debug" background
        if(layers.empty())
        {
            _prepare_texture(_d->smpte.get());
            auto sdltex = static_cast<SDL_Texture*>(_d->smpte->rptr);
            SDL_SetTextureScaleMode(sdltex, SDL_SCALEMODE_LINEAR);
            SDL_RenderTexture(_d->render, static_cast<SDL_Texture*>(_d->smpte->rptr), NULL, NULL);
        }

        // update render target sizing calculations if needed, again
        // some may have been added during the update phases
        if(_d->rt_sizes_dirty)
            _targets_resize();

        // adjust layer viewports that follow targets
        for(auto &layer : layers)
        {
            if(layer.follow_target)
            {
                auto it = _d->targets.find(layer.target_id);
                if(it!=_d->targets.end())
                {
                    auto &tgt = it->second;
                    layer.viewport = {0, 0, tgt.curr_w, tgt.curr_h};
                }
            }
        }

        //now render all layers
        for(const auto &layer : layers)
        {
            const auto &vp = layer.viewport;

            // setup render target
            if(layer.target_id == render::TARGET_INVALID)
                continue;
            else if(layer.target_id == render::TARGET_DEFAULT)
            {
                SDL_SetRenderTarget(_d->render, nullptr);
            }
            else
            {
                // we have a target for this layer
                auto it = _d->targets.find(layer.target_id);
                if(it!=_d->targets.end())
                {
                    auto &tgt = it->second;

                    auto col = tgt.color_tex;
                    if(col)
                    {
                        auto changed = _prepare_texture(col.get()); // (re) create texture or apply size
                        if(!col->rptr)
                            continue;   // cannot continue
                        SDL_SetRenderTarget(_d->render, static_cast<SDL_Texture*>(col->rptr));
                        if(changed && tgt.desc.init_clear && !layer.clear)
                        {
                            // clear target if texture was recreated and flag is set
                            SDL_SetRenderDrawColor(_d->render, 0, 0, 0, 255);
                            SDL_SetRenderDrawBlendMode(_d->render, SDL_BLENDMODE_NONE);
                            SDL_FRect area{ 0.f, 0.f, (float)col->width, (float)col->height };
                            SDL_RenderFillRect(_d->render, &area);
                        }
                    }
                    else
                    {
                        log::warn("[render_2d] target has no color texture, skipping: %u",
                                  layer.target_id);
                        continue;
                    }
                }
                else
                {
                    log::warn("[render_2d] layer has invalid target id, skipping: order=%i target_id=%u",
                              layer.order, layer.target_id);
                    continue;
                }
            }

            // Clear viewport region if requested
            if(layer.clear)
            {

                //log::info("CLEAR %dx%d @ %d,%d", vp.w, vp.h, vp.x, vp.y);
                SDL_SetRenderDrawColor(_d->render,
                    static_cast<Uint8>(layer.clear_r * 255), static_cast<Uint8>(layer.clear_g * 255),
                                       static_cast<Uint8>(layer.clear_b * 255), 255);
                SDL_SetRenderDrawBlendMode(_d->render, SDL_BLENDMODE_NONE);
                SDL_FRect clip { static_cast<float>(vp.x), static_cast<float>(vp.y),
                                    static_cast<float>(vp.w), static_cast<float>(vp.h) };
                SDL_RenderFillRect(_d->render, &clip);
            }

            if(layer.custom_2d_draw)
            {
                // this layer has a custom 2d drawing callback
                // batch and render that instead
                _d->batcher.clear();
                render::clip_t clip_rect { (float) vp.x, (float) vp.y, (float) vp.w, (float) vp.h };
                layer.custom_2d_draw(layer, _d->batcher);
                _draw_batches(_d->batcher, clip_rect);
            }
            else
            {
                // regular scene draw
                auto *sc = engine::instance().find_scene(layer.scene_id);
                if(!sc)
                    continue;   // layer has no scene, skip

                // Find camera and determine world bounds
                auto &reg = sc->registry();
                glm::vec4 world_bounds {vp.x, vp.y, vp.w, vp.h};
                if(layer.camera != entt::null)
                {
                    float cam_cx = 0.0f, cam_cy = 0.0f;
                    auto *sp  = reg.try_get<cspatial>(layer.camera);
                    auto *cam = reg.try_get<ccamera>(layer.camera);
                    if(sp)  { cam_cx = sp->pos.x; cam_cy = sp->pos.y; }
                    if(cam) { world_bounds = cam->cam2d.calc_world_bounds(cam_cx, cam_cy, vp); }
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

                // calculated view projection
                glm::mat4 viewproj = view * proj;

                _draw_scene(*sc, viewproj, layer); // takes care of clipping
            }
        }

        // reset any render target association
        SDL_SetRenderTarget(_d->render, nullptr);

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
        if(!_d->has_ui)
        {
            // if we have no UI, do this here
            _d->ui_vp = {0, 0, _d->wx, _d->wy};
            _d->rt_sizes_dirty = true;
            // otherwise, this will get done in PRE_RENDER phase from ui info
        }
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

    auto clip = render::clip_t{
        static_cast<float>(l.viewport.x),
        static_cast<float>(l.viewport.y),
        static_cast<float>(l.viewport.w),
        static_cast<float>(l.viewport.h)
    };

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

bool render_2d::_prepare_texture(rtexture *rtex)
{
    assert(rtex);  // this shuldd never happen


    auto sdltex = static_cast<SDL_Texture*>(rtex->rptr);

    if(rtex->rtarget)
    {
        // this texture is a render target

        bool changed {false};
        bool destroy {false};
        bool create {false};

        // validate basic requested size
        if(rtex->width == 0 || rtex->height == 0)
        {
            log::warn("[render_2d] preparing rt texture for 0x%08x: zero size!", rtex->id());
                // a render target of size zero should not exist
                // still, if it had a valid texture before, we will still destroy it
            destroy = true;
        }
        else if(sdltex && sdltex->w == rtex->width && sdltex->h == rtex->height)
        {
            // target texture exists and size matches, nothing to do
            return changed;
        }
        else if(!sdltex)
        {
            // there is no texture, create one
            create = true;
        }
        else
        {
            // texture exists but size does not match, recreate
            destroy = true;
            create = true;
        }

        if(destroy && sdltex)
        {
            SDL_DestroyTexture(sdltex);
            rtex->rptr = sdltex = nullptr;
            changed = true;
        }

        if(create && !sdltex)
        {
            // since we only support color formats, let the renderer pick an optimal RGBA format
            // for that, we don't need to pass any format
            auto props = SDL_CreateProperties();
            SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, rtex->width);
            SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, rtex->height);
            SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_TARGET);
            rtex->rptr = sdltex = SDL_CreateTextureWithProperties(_d->render, props);
            if(sdltex)
            {
                log::info("[render_2d] rt texture created for 0x%08x: %dx%d", rtex->id(), rtex->width, rtex->height);
                SDL_ScaleMode sm = rtex->nearest?
                SDL_SCALEMODE_NEAREST : _d->default_tex_scalemode;
                SDL_SetTextureScaleMode(sdltex, sm);
                changed = true;
            }
            else
            {
                log::warn("[render_2d] rt texture creation failure for 0x%08x: '%s'", rtex->id(), SDL_GetError());
            }
        }

        return changed;
    }

    // common texture
    // check if we need to do an upload, potentially recreating the texture
    if (!rtex->uploaded && rtex->surf)
    {
        if(sdltex)
        {
            // destroy existing texture if needed
            SDL_DestroyTexture(sdltex);
            sdltex = nullptr;
            rtex->rptr = nullptr;
        }

        // always create a new texture
        // we could check for max texture size but SDL already does it
        rtex->rptr = sdltex = SDL_CreateTextureFromSurface(_d->render, rtex->surf);
        log::info("[render_2d] texture created for 0x%08x: %dx%d", rtex->id(), rtex->width, rtex->height);
        // register its cleanup on resource deletion
        rtex->on_delete = &_texture_cleanup;
        rtex->on_delete_uptr = this;

        if(sdltex)
        {
            SDL_ScaleMode sm = rtex->nearest?
                            SDL_SCALEMODE_NEAREST : _d->default_tex_scalemode;
            SDL_SetTextureScaleMode(sdltex, sm);
            rtex->uploaded = true;
        }
        else
            log::warn("[render_2d] texture upload failure for 0x%08x: '%s'", rtex->id(), SDL_GetError());

        // even on upload failure, we destroy the surface, to prevent continuous failure every frame
        SDL_DestroySurface(rtex->surf);
        rtex->surf = nullptr;
        return true;
    }
    return false;
}

entt::entity render_2d::pick(const render_layer &layer, float vp_x, float vp_y)
{
    // just forward to default cpu picker
    return _d->picker.pick(layer, vp_x, vp_y);
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
        tex.rptr = nullptr; // for correctness, even if irrelevant by now
    }
}

// RTT interface


render::target_id_t render_2d::target_create(const render::target_desc& desc)
{
    // first, let's do some sanity checks on the descriptor

    if (desc.has_depth)
        return render::TARGET_INVALID;  // sorry, I'm shallow

    if (desc.size_mode == render::target_size_mode::ABSOLUTE)
    {
        if(!(desc.abs_width && desc.abs_height))
            return render::TARGET_INVALID; // absolute sizing gone wild
        if(desc.abs_width > _d->r_prop_tex_max_sz)
            return render::TARGET_INVALID; // greedy
        if(desc.abs_height > _d->r_prop_tex_max_sz)
            return render::TARGET_INVALID; // greedy too
    }
    else
    {
        if (desc.size_scale <= 0.0f)
            return render::TARGET_INVALID; // relative sizing gone wild

        if(desc.size_mode == render::target_size_mode::TARGET_RELATIVE)
            if(!_d->targets.contains(desc.size_source))
                return render::TARGET_INVALID; // don't know her
    }

    // ok, descriptor checks out
    // let's build it'
    auto target_id = _d->rt_next_id++;
    render_2d_target target {};
    target.id = target_id;
    target.desc = desc;

    std::string rname {"r_2d_t_"};
    rname += std::to_string(target.id);
    auto rid = entt::hashed_string(rname.c_str()).value();
    target.color_tex = std::make_shared<rtexture>( rid );
    target.color_tex->rtarget = true;
    target.color_tex->on_delete = &_texture_cleanup;
    target.color_tex->on_delete_uptr = _d.get();

    if(desc.size_mode == render::target_size_mode::ABSOLUTE)
    {
        // preload desired static size
        // _prepare_texture does the rest
        target.color_tex->width = desc.abs_width;
        target.color_tex->height = desc.abs_height;
    }
    else
    {
        // we need to recalculate resize order and sizes themselves
        _d->rt_resize_order_dirty = true;
        _d->rt_sizes_dirty = true;
    }

    _d->targets.emplace(target.id, std::move(target));

    return target_id;
}

bool render_2d::target_destroy(render::target_id_t id)
{
    if (id == render::TARGET_DEFAULT || id == render::TARGET_INVALID)
        return false;

    auto it = _d->targets.find(id);
    if (it == _d->targets.end())
        return false;

    // ok, target exists, let's get rid of it

    // removing a target that has size dependants may cause issues
    // but that is a separate issue
    // just mark the graph dirty
    if(it->second.desc.size_mode != render::target_size_mode::ABSOLUTE)
    {
        _d->rt_resize_order_dirty = true;
    }

    // NOTE that the texture resource is destroyed via shared ptr semantics
    _d->targets.erase(it);

    return true;
}

std::shared_ptr<rtexture> render_2d::target_get_color_texture(render::target_id_t id) const
{
    if (id == render::TARGET_DEFAULT || id == render::TARGET_INVALID)
        return {nullptr};

    auto it = _d->targets.find(id);
    if (it == _d->targets.end())
        return {nullptr};

    return it->second.color_tex;
}

std::shared_ptr<rtexture> render_2d::target_get_depth_texture(render::target_id_t id) const
{
    // we don't have nor use depth textures on the 2d backend
    return {nullptr};
}

glm::ivec2 render_2d::target_get_size(render::target_id_t id) const
{
    static constexpr glm::vec2 INVALID = {-1.f, -1.f};

    if (id == render::TARGET_DEFAULT || id == render::TARGET_INVALID)
        return INVALID;

    auto it = _d->targets.find(id);
    if (it == _d->targets.end())
        return INVALID;

    return {it->second.curr_w, it->second.curr_h};
}

bool render_2d::target_has_depth(render::target_id_t id) const
{
    // No 2D targets have a depth buffer.
    // SDL_Renderer does not use the Z axis, so we cannot make use
    // of the depth buffer in any meaningful way.
    return false;
}


void render_2d::_targets_sizing_reorder()
{
    // map all targets to a sequential vertex index
    std::unordered_map<render::target_id_t, int> mapping;
    int next_mapping = 0;
    mapping[render::TARGET_DEFAULT] = next_mapping++;
    for(const auto &[tid, target]: _d->targets)
    {
        if(!mapping.contains(tid))
            mapping[tid] = next_mapping++;
        if(target.desc.size_mode == render::target_size_mode::TARGET_RELATIVE)
            if(!mapping.contains(target.desc.size_source))
                mapping[target.desc.size_source] = next_mapping++;
    }

    // build a graph using our mapping
    entt::adjacency_matrix<entt::directed_tag> graph {mapping.size()};
    for(const auto &[tid, target]: _d->targets)
    {
        switch (target.desc.size_mode) {
            case render::target_size_mode::ABSOLUTE:
                break; // no dependencies

            case render::target_size_mode::UI_RELATIVE:
                graph.insert(mapping[render::TARGET_DEFAULT], mapping[tid]);
                break;

            case render::target_size_mode::TARGET_RELATIVE:
                graph.insert(mapping[target.desc.size_source], mapping[tid]);
                break;
        }
    }

    // now order graph via topo sort
    auto ok = _d->rt_resize_sorter.build(graph);
    if(!ok)
        log::error("[render_2d] _targets_sizing_reorder: cyclic dependencies in render target sizes");

    // finally, remap vertex indices from graph back into target ids
    std::unordered_map<unsigned long long, render::target_id_t> invmap;
    for(auto &&[tid, vtx]:mapping)
        invmap[vtx] = tid;
    _d->rt_resize_order.clear();
    for(auto vtx: _d->rt_resize_sorter.execution_order())
    {
        auto tid = invmap[vtx];
        log::warn("VTX %d TID %d", (int) vtx, (int) tid);
        _d->rt_resize_order.push_back(tid);
    }

    _d->rt_resize_order_dirty = false;
}


void render_2d::_targets_resize()
{
    if(_d->rt_resize_order_dirty)
        _targets_sizing_reorder();

    // calculate render target sizes
    // GPU texture sizes will be applied before rendering
    for(auto tid: _d->rt_resize_order)
    {
        if(tid == render::TARGET_DEFAULT)
        {
            // main swapchain, our graph "anchor"
            // ui_vp should already be up to date, so do noting
            continue;
        }

        auto it = _d->targets.find(tid);
        if(it == _d->targets.end())
        {
            log::error("[render_2d] _targets_resize: unknown target id: %u", tid);
            continue;
        }

        auto &target = it->second;
        switch(target.desc.size_mode)
        {
            case render::target_size_mode::ABSOLUTE:
                target.curr_w = std::max(1u, target.desc.abs_width);
                target.curr_h = std::max(1u, target.desc.abs_height);
                break;

            case render::target_size_mode::UI_RELATIVE:
                target.curr_w = std::max(1u, static_cast<unsigned>(
                    std::ceil(_d->ui_vp.z * target.desc.size_scale)));
                target.curr_h = std::max(1u, static_cast<unsigned>(
                    std::ceil(_d->ui_vp.w * target.desc.size_scale)));
                break;

            case render::target_size_mode::TARGET_RELATIVE:
            {
                const auto oid = target.desc.size_source;
                auto it_other = _d->targets.find(oid);
                if(it == _d->targets.end())
                {
                    log::error("[render_2d] _targets_resize: %u: unknown source target id: %u. setting to 1x1", tid, oid);
                    target.curr_w = target.curr_h = 1;
                }
                else
                {
                    auto &other = it_other->second;
                    target.curr_w = std::max(1u, static_cast<unsigned>(
                        std::ceil(other.curr_w * target.desc.size_scale)));
                    target.curr_h = std::max(1u, static_cast<unsigned>(
                        std::ceil(other.curr_h * target.desc.size_scale)));
                }
            }
            break;
        }

        // apply newly calculated sizes to the texture resources
        // we defer GPU texture manageent for the rendering phase
        target.color_tex->width = target.curr_w;
        target.color_tex->height = target.curr_h;
    }

    // TODO with render targets having correct sizes, we can update
    //      any viewports that depend on those

    // finally, clear the dirty flag
    _d->rt_sizes_dirty = false;
}



// RTTI metadata
extern "C" void _rtti_init_render_2d()
{
    entt::meta_factory<nb::render_2d>{}
        .type("render_2d"_hs)
        .custom<rtti::type_info>(rtti::type_info{"render_2d", rtti::TYPE_CLASS_SYSTEM})
        .base<nb::system>()
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
}
