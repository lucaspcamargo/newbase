#include <newbase/sys/render_simple/render_simple.hpp>
#include <newbase/engine.hpp>
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

// new render components
// merge above after cleanup
#include <newbase/render/window.hpp>
#include <newbase/render/batcher2d.hpp>
#include <newbase/render/collector2d.hpp>

#include "SDL3/SDL_render.h"
#include "glm/fwd.hpp"
#include "imgui.h"
//#include "imgui_internal.h" // for ImGuiViewport
#include "backends/imgui_impl_sdl3.h"
#include "../../ui/imgui_impl_sdlrenderer3.h"
#include "newbase/layer.hpp"
#include "newbase/services/renderer_service.hpp"
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


struct viewport_entry {
    int x, y, w, h;
    bool clear;
    float r, g, b, a;
};

struct nb::render_simple_p
{
    render::window rwin;
    SDL_Window    *_win {nullptr};
    SDL_Renderer  *_render {nullptr};
    int            _wx {0}, _wy {0};
    float          _scale {1.0};
    SDL_Rect       _safe {};
    float          _clear_r{0.f}, _clear_g{0.f}, _clear_b{0.f};

    // fallback camera used when no render layers are configured
    ccamera  _fallback_camera  {};
    cspatial _fallback_spatial {};

    std::unordered_map<viewport_handle, viewport_entry> _viewports;
    viewport_handle _next_vp_handle { 1 }; // 0 is VIEWPORT_INVALID

    // The default viewport covers the window's scene area.
    // It is auto-sized to the full window on init and resized unless
    // a caller has explicitly overridden it via update_viewport().
    viewport_handle _default_vp       { VIEWPORT_INVALID };
    bool            _default_vp_owned { false }; // true once set by an external caller
                                                 // if false, we will take care of it instead

    std::vector<SDL_Vertex> _xform_buf {};
    bool _has_ui {false};

    render::batcher2d batcher;
    render::collector2d collector;

    // try to get a pointer to a registered viewport
    // or nullptr, if invalid or non-existant
    viewport_entry*  try_find_viewport(viewport_handle vphnd)
    {
        if (vphnd == VIEWPORT_INVALID)
            return nullptr;
        auto it = _viewports.find(vphnd);
        if(it != _viewports.end())
            return &(it->second);
        return nullptr;
    }
};



// HACK: temporary editor grid — to be replaced with a proper grid layer/component
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
#ifdef TRACY_ENABLE
static SDL_Surface *_tracyCopy {nullptr};
#endif


render_simple::render_simple()
{
    log::info("[render_simple] constructed");
    
    _d = std::make_unique<render_simple_p>();

    // register services
    entt::locator<renderer_service*>::emplace(this);
    entt::locator<picker_service*>::emplace(this);
}

render_simple::~render_simple()
{
    log::info("[render_simple] destroying");
    if(_d->_has_ui)
    {
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ui_manager* ui_mgr = entt::locator<ui_manager*>::value();
        if(ui_mgr)
        {
            ui_mgr->ui_destroy();
        }
        else
            log::warn("[render_simple] could not locate ui service for destruction");
    }
    if(_d->_render)
    {
        SDL_DestroyRenderer(_d->_render);
    }

    // release our private data
    // render::window is destroyed in tandem
    _d.reset();

    log::info("[render_simple] destroyed");
}


SDL_InitFlags render_simple::sdl_subsystems(ryml::ConstNodeRef cfg)
{
    // TODO doesn't seem to work in runtime
    // No SDL hints have found for this either
    // also msvcpp does not like env manipulation on windows
    if(cfg.has_child("prefer") && !cfg["prefer"].invalid())
    {
        log::info("[render_simple] current driver: %s", SDL_GetCurrentVideoDriver());
        bool already = getenv("SDL_VIDEODRIVER");
        std::string preferred;
        cfg["prefer"] >> preferred;
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, preferred.c_str());
        log::info("[render_simple] preferring: %s%s", preferred.c_str(), already?" (overrides env)":"");
    }
    return SDL_INIT_VIDEO;
}


bool render_simple::init(ryml::ConstNodeRef cfg)
{
    log::info("[render_simple] init");
    const auto num_drivers = SDL_GetNumRenderDrivers();

    log::info("[render_simple] current driver: %s", SDL_GetCurrentVideoDriver());
    

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

        log::info("[render_simple] available drivers: %s", driver_names.c_str());
        }
    }

    if(!_d->rwin.create(cfg, SDL_WINDOW_OPENGL))
    {
        log::error("[render_simple] window creation failed");
        return false;
    }

    _d->_win = _d->rwin.get();
    log::info("[render_simple] window scale: %f", _d->_scale);

    _d->_render = SDL_CreateRenderer(_d->_win, nullptr);
    log::info("[render_simple] renderer: %s", SDL_GetRendererName(_d->_render));

    SDL_SetRenderVSync(_d->_render, 1);
    if (_d->_render == nullptr)
    {
        log::error("[render_simple] SDL_CreateRenderer(): %s\n", SDL_GetError());
        return false;
    }
    else
        log::info("[render_simple] created renderer: %s", SDL_GetRendererName(_d->_render));

#ifndef NEWBASE_WII
    SDL_SetWindowPosition(_d->_win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
#endif

    if(!_d->rwin.show())
    {
        log::error("[render_simple] window show() filed: %s\n", SDL_GetError());
        return false;
    }

    // get main render window attributes
    _d->_wx = _d->rwin.width();
    _d->_wy = _d->rwin.height();
    _d->_scale = _d->rwin.ui_scale();
    _d->_safe = _d->rwin.safe_area();

    // attempt to load and set window icon
    auto icon_tex = rman().get<rtexture>("_nb_core/icon_192.png"_hs);
    if(icon_tex && icon_tex->surf)
    {
        SDL_SetWindowIcon(_d->_win, icon_tex->surf);
    }

    // init gui via ui manager
    ui_manager* ui_mgr = entt::locator<ui_manager*>::value();
    if(ui_mgr)
    {
        _d->_has_ui = ui_mgr->ui_init();
    }
    else
        log::warn("[render_simple] could not init ui via ui_manager service");
    
    if(_d->_has_ui)
    {
        // Setup Platform/Renderer backends
        ImGui_ImplSDL3_InitForSDLRenderer(_d->_win, _d->_render);
        ImGui_ImplSDLRenderer3_Init(_d->_render);

        ui_mgr->ui_init_finish(_d->_scale);
    }
    else
    {
        log::warn("[render_simple] no ui, not initializing ImGui renderer");
    }



    // Create the persistent default viewport (full window, no clear).
    _d->_default_vp = create_viewport(0, 0, _d->_wx, _d->_wy, false);
    log::info("[render_simple] default viewport: %u", _d->_default_vp);

    return true;
}

bool render_simple::step(nb::step_phase phase)
{
    if(phase == step_phase::PRE_UPDATE)
    {
        ZoneScopedN("RenderPreUpdate");
        // Start the Dear ImGui frame
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ui_manager* ui_mgr = entt::locator<ui_manager*>::value();
        if(ui_mgr)
        {
            ui_mgr->ui_new_frame(_d->_safe.x, _d->_safe.y, _d->_safe.w, _d->_safe.h);
        }
    }
    else if(phase == step_phase::POST_UPDATE)
    {
    }
    else if(phase == step_phase::PRE_RENDER)
    {
        ZoneScopedN("RenderPre");
    }
    else if(phase == step_phase::RENDER)
    {
        ZoneScopedN("Render");

        const auto &layers = engine::instance().render_layers();

        // Full-screen clear — use first layer's clear color if present
        {
            float cr = _d->_clear_r, cg = _d->_clear_g, cb = _d->_clear_b;
            if (!layers.empty() && layers.front().clear_bg)
            {
                cr = layers.front().clear_r;
                cg = layers.front().clear_g;
                cb = layers.front().clear_b;
            }
            SDL_SetRenderDrawColor(_d->_render,
                static_cast<Uint8>(cr * 255), static_cast<Uint8>(cg * 255),
                static_cast<Uint8>(cb * 255), 255);
            SDL_RenderClear(_d->_render);
        }

        if(layers.empty())
        {
            log::verb("[render] no render layers, using fallback path");
            // Fallback: draw default scene through the default viewport.
            auto dvp_it = _d->_viewports.find(_d->_default_vp);
            const viewport_entry &dvp = (dvp_it != _d->_viewports.end())
                ? dvp_it->second
                : viewport_entry{ 0, 0, _d->_wx, _d->_wy, false, 0, 0, 0, 1 };

            float cam_cx = _d->_fallback_spatial.pos.x;
            float cam_cy = _d->_fallback_spatial.pos.y;
            float zoom   = _d->_fallback_camera.zoom > 0.f ? _d->_fallback_camera.zoom : 1.f;
            float vp_cx  = dvp.x + dvp.w * 0.5f;
            float vp_cy  = dvp.y + dvp.h * 0.5f;
            glm::mat4x4 viewproj =
                glm::translate(glm::mat4x4{1.0f}, glm::vec3{vp_cx, vp_cy, 0.f}) *
                glm::scale(glm::mat4x4{1.0f}, glm::vec3{zoom, zoom, 1.f}) *
                glm::translate(glm::mat4x4{1.0f}, glm::vec3{-cam_cx, -cam_cy, 0.f});
            SDL_Rect clip_rect { dvp.x, dvp.y, dvp.w, dvp.h };
            SDL_SetRenderClipRect(_d->_render, &clip_rect);
            // auto &reg = engine::instance().default_scene().registry();
            //_draw_scene(reg, viewproj, 0xFFFFFFFF);
            render_layer tmp_l {};
            _draw_scene(engine::instance().default_scene(), viewproj, tmp_l);
            SDL_SetRenderClipRect(_d->_render, nullptr);
        }
        else
        {
            for(const auto &layer : layers)
            {
                auto *sc = engine::instance().find_scene(layer.scene_id);
                if(!sc)
                    continue;   // layer has no scene, skip

                auto vp_ptr = _d->try_find_viewport(layer.viewport);
                if(!vp_ptr)
                    continue;   // layer has no viewport, skip
                const auto &vp = *vp_ptr;

                // Clear viewport region if requested
                if(vp.clear)
                {
                    SDL_SetRenderDrawColor(_d->_render,
                        static_cast<Uint8>(vp.r * 255), static_cast<Uint8>(vp.g * 255),
                        static_cast<Uint8>(vp.b * 255), static_cast<Uint8>(vp.a * 255));
                    SDL_FRect clip { static_cast<float>(vp.x), static_cast<float>(vp.y),
                                     static_cast<float>(vp.w), static_cast<float>(vp.h) };
                    SDL_RenderFillRect(_d->_render, &clip);
                }

                // Build camera transform from camera entity
                auto &reg = sc->registry();
                float cam_cx = 0.f, cam_cy = 0.f, zoom = 1.f;
                if(layer.camera != entt::null)
                {
                    auto *sp  = reg.try_get<cspatial>(layer.camera);
                    auto *cam = reg.try_get<ccamera>(layer.camera);
                    if(sp)  { cam_cx = sp->pos.x; cam_cy = sp->pos.y; }
                    if(cam) { zoom = cam->zoom; }
                    log::verb("[render] layer cam=%u sp=%p cam=%p zoom=%.2f cx=%.0f cy=%.0f",
                        entt::to_integral(layer.camera), sp, cam, zoom, cam_cx, cam_cy);
                }

                float vp_cx = vp.x + vp.w * 0.5f;
                float vp_cy = vp.y + vp.h * 0.5f;
                glm::mat4x4 viewproj =
                    glm::translate(glm::mat4x4{1.0f}, glm::vec3{vp_cx, vp_cy, 0.f}) *
                    glm::scale(glm::mat4x4{1.0f}, glm::vec3{zoom, zoom, 1.f}) *
                    glm::translate(glm::mat4x4{1.0f}, glm::vec3{-cam_cx, -cam_cy, 0.f});

                SDL_Rect clip_rect { vp.x, vp.y, vp.w, vp.h };
                SDL_SetRenderClipRect(_d->_render, &clip_rect);
                if (layer.use_grid)
                {
                    SDL_SetRenderDrawColor(_d->_render,
                        static_cast<Uint8>(layer.clear_r * 255),
                        static_cast<Uint8>(layer.clear_g * 255),
                        static_cast<Uint8>(layer.clear_b * 255), 255);
                    SDL_FRect fill { (float)vp.x, (float)vp.y, (float)vp.w, (float)vp.h };
                    SDL_RenderFillRect(_d->_render, &fill);
                    _draw_editor_grid_hack(_d->_render, cam_cx, cam_cy, zoom, vp.x, vp.y, vp.w, vp.h);
                }
                _draw_scene(*sc, viewproj, layer);
                SDL_SetRenderClipRect(_d->_render, nullptr);
            }
        }
        
        // GUI
        ui_manager* ui_mgr = entt::locator<ui_manager*>::value();
        if(ui_mgr)
        {
            ZoneScopedN("RenderDrawUI");
            ui_mgr->draw_tool_windows();
            ui_mgr->draw_perf();
        }
        ImGui::Render();

#ifndef ANDROID
        if(_d->_scale != 1.0f)
            SDL_SetRenderScale(_d->_render, _d->_scale, _d->_scale);
#endif
        // fixed overlays
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), _d->_render);
        // reset render scale for HiDPI
#ifndef ANDROID
        if(_d->_scale != 1.0f)
            SDL_SetRenderScale(_d->_render, 1.0f, 1.0f);
#endif

#ifdef TRACY_ENABLED
        // TODO
        // rebuild _tracyCopy to be able to hold current framebuffer (if needed)
        // Call SDL_RenderReadPixels() to fill buffer
        // Send buffer to tracy profiler
#endif
        SDL_RenderPresent(_d->_render);
        FrameMark;
    }
    return true;
}

bool render_simple::event( SDL_Event * evt)
{
    ImGui_ImplSDL3_ProcessEvent(evt);

    bool window_update = _d->rwin.event(evt);
    if(window_update)
    {
        _d->_wx = _d->rwin.width();
        _d->_wy = _d->rwin.height();
        _d->_scale = _d->rwin.ui_scale();
        _d->_safe = _d->rwin.safe_area();

        if(!_d->_default_vp_owned && _d->_default_vp != VIEWPORT_INVALID)
            update_viewport(_d->_default_vp, 0, 0, _d->_wx, _d->_wy);
        if(_d->_fallback_camera.wmax > 0.f)
            cam_2d_setup(_d->_fallback_spatial.pos.x, _d->_fallback_spatial.pos.y,
                            _d->_fallback_camera.wmax, _d->_fallback_camera.hmax);
    }

    if(evt->type == SDL_EVENT_KEY_DOWN && evt->key.scancode == SDL_SCANCODE_F11)
        SDL_SetWindowFullscreen(_d->_win, !(SDL_GetWindowFlags(_d->_win)&SDL_WINDOW_FULLSCREEN));

    return true;
}




void render_simple::_draw_scene(scene &scene, const glm::mat4x4 &viewproj, const render_layer &l)
{
    _d->batcher.clear();
    _d->collector.clear();

    const auto bounds = _get_viewport_bounds(l);
    (void) bounds;

    _d->collector.collect(_d->batcher, scene, l, viewproj);

    const auto &data = _d->batcher.data();
    for (const auto &cmd: _d->batcher.commands())
    {
        assert(cmd.index_count);
        assert(cmd.index_start + cmd.index_count <= data.inds.size());
        assert(cmd.base_vertex < data.verts.size());

        const render::vertex2d *vtx0 = data.verts.data() + cmd.base_vertex;
        const uint16_t *ind0 = data.inds.data() + cmd.index_start;
        constexpr auto v_stride = sizeof(render::vertex2d);

        auto rtex = cmd.texture != -1? data.tex[cmd.texture].get() : nullptr;
        auto sdltex = rtex? rtex->tex : nullptr;

        if (!sdltex && !rtex->uploaded && rtex->surf)
        {
            rtex->tex = sdltex = SDL_CreateTextureFromSurface(_d->_render, rtex->surf);
            if(sdltex)
            {
                SDL_SetTextureScaleMode(rtex->tex, SDL_SCALEMODE_NEAREST);
                rtex->uploaded = true;
            }
            else
                log::warn("[render_simple] texture upload failure for 0x%08x", rtex->id());

            // even on upload failure, we destroy the surface, to prevent continuous failure every frame
            SDL_DestroySurface(rtex->surf);
            rtex->surf = nullptr;
        }
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
            SDL_GetRenderDrawBlendMode(_d->_render, &blend_prev);
            SDL_SetRenderDrawBlendMode(_d->_render, blend_now);
        }

        // OUR DRAW CALL
        SDL_RenderGeometryRaw(_d->_render, sdltex,
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
            SDL_SetRenderDrawBlendMode(_d->_render, blend_prev);
        }
    }

    SDL_SetRenderDrawBlendMode(_d->_render, SDL_BLENDMODE_BLEND);

}


std::pair<glm::vec2, glm::vec2> render_simple::_get_viewport_bounds(const render_layer &l)
{
    // initialize with window dimensions
    glm::vec2 tl {0.0, 0.0};
    glm::vec2 br {static_cast<float>(_d->rwin.width()), static_cast<float>(_d->rwin.height())};

    auto vp = _d->try_find_viewport(l.viewport);
    if(vp)
    {
        // layer has a vaid viewport set, use that instead
        tl = {vp->x, vp->y};
        br = {vp->w, vp->h};
    }

    return std::make_pair(tl, br);
}


entt::entity render_simple::pick(const render_layer &layer, float vp_x, float vp_y)
{
    // TODO move to render::picker2d, generalizedd
    auto *sc = engine::instance().find_scene(layer.scene_id);
    if (!sc) { log::warn("[pick] no scene"); return entt::null; }

    auto it = _d->_viewports.find(layer.viewport);
    if (it == _d->_viewports.end()) { log::warn("[pick] viewport %u not found", layer.viewport); return entt::null; }
    const auto &vp = it->second;

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
                else if (spr.tex && spr.tex->uploaded)
                    dims = { (float)spr.tex->tex->w, (float)spr.tex->tex->h };
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
    return best;
}

void render_simple::cam_2d_setup(float cx, float cy, float wmax, float hmax)
{
    _d->_fallback_spatial.pos = { cx, cy, 0.f };
    _d->_fallback_camera.wmax = wmax;
    _d->_fallback_camera.hmax = hmax;

    float scale_x = _d->_wx / wmax;
    float scale_y = _d->_wy / hmax;
    _d->_fallback_camera.zoom = std::min(scale_x, scale_y);

    log::verb("[render_simple] cam2d setup: cx=%f cy=%f wmax=%f hmax=%f => zoom=%f",
        cx, cy, wmax, hmax, _d->_fallback_camera.zoom);
}

float render_simple::cam_2d_scale()
{
    return _d->_fallback_camera.zoom;
}

bool render_simple::get_2d_extents(renderer_service::extents_2d &extents)
{
    // Prefer the first configured render layer's camera
    // TODO better control fo this mapping
    const auto &layers = engine::instance().render_layers();
    if(!layers.empty())
    {
        const auto &layer = layers.front();
        auto *sc = engine::instance().find_scene(layer.scene_id);
        auto it  = _d->_viewports.find(layer.viewport);
        if(sc && it != _d->_viewports.end())
        {
            auto &reg = sc->registry();
            auto &vp  = it->second;
            float cx = 0.f, cy = 0.f, zoom = 1.f;
            if(layer.camera != entt::null)
            {
                if(auto *sp  = reg.try_get<cspatial>(layer.camera)) { cx = sp->pos.x; cy = sp->pos.y; }
                if(auto *cam = reg.try_get<ccamera>(layer.camera))  { zoom = cam->zoom; }
            }
            float span_x = vp.w / zoom;
            float span_y = vp.h / zoom;

            // on android, the ui style and font are scaled
            // but the internal imgui scale remains at 1.0
#ifdef ANDROID
            static constexpr float ui_scale = 1.0f;
#else
            float ui_scale = _d->_scale;
#endif
            extents = { vp.w, vp.h, span_x, span_y,
                cx - span_x * 0.5f, cy - span_y * 0.5f,
                cx + span_x * 0.5f, cy + span_y * 0.5f,
                ui_scale, vp.x, vp.y };
            return true;
        }
    }

    // on android, the ui style and font are scaled
    // but the internal imgui scale remains at 1.0
#ifdef ANDROID
    static constexpr float ui_scale = 1.0f;
#else
    float ui_scale = _d->_scale;
#endif

    // Fallback: use the default viewport's current rect.
    auto dvp_it = _d->_viewports.find(_d->_default_vp);
    int dvp_w = (dvp_it != _d->_viewports.end()) ? dvp_it->second.w : _d->_wx;
    int dvp_h = (dvp_it != _d->_viewports.end()) ? dvp_it->second.h : _d->_wy;
    float zoom   = _d->_fallback_camera.zoom > 0.f ? _d->_fallback_camera.zoom : 1.f;
    float cx     = _d->_fallback_spatial.pos.x;
    float cy     = _d->_fallback_spatial.pos.y;
    float span_x = dvp_w / zoom;
    float span_y = dvp_h / zoom;
    int dvp_x = (dvp_it != _d->_viewports.end()) ? dvp_it->second.x : 0;
    int dvp_y = (dvp_it != _d->_viewports.end()) ? dvp_it->second.y : 0;
    extents = { dvp_w, dvp_h, span_x, span_y,
        cx - span_x * 0.5f, cy - span_y * 0.5f,
        cx + span_x * 0.5f, cy + span_y * 0.5f,
        ui_scale, dvp_x, dvp_y };
    return true;
}

viewport_handle render_simple::create_viewport(int x, int y, int w, int h,
                                               bool clear, float r, float g, float b, float a)
{
    viewport_handle handle = _d->_next_vp_handle++;
    _d->_viewports[handle] = { x, y, w, h, clear, r, g, b, a };
    log::info("[render_simple] viewport %u created: %dx%d@%d,%d", handle, w, h, x, y);
    return handle;
}

void render_simple::update_viewport(viewport_handle vp, int x, int y, int w, int h)
{
    auto it = _d->_viewports.find(vp);
    if(it == _d->_viewports.end()) return;
    it->second.x = x; it->second.y = y;
    it->second.w = w; it->second.h = h;

    if(vp == _d->_default_vp)
    {
        _d->_default_vp_owned = true;
        // Recompute zoom to fit the new viewport dimensions.
        if(_d->_fallback_camera.wmax > 0.f && w > 0 && h > 0)
        {
            _d->_fallback_camera.zoom = std::min(
                float(w) / _d->_fallback_camera.wmax,
                float(h) / _d->_fallback_camera.hmax);
        }
    }
}

void render_simple::destroy_viewport(viewport_handle vp)
{
    _d->_viewports.erase(vp);
}

viewport_handle render_simple::default_viewport() const
{
    return _d->_default_vp;
}

void render_simple::reset_default_viewport()
{
    _d->_default_vp_owned = false;
    if(_d->_default_vp != VIEWPORT_INVALID)
        update_viewport(_d->_default_vp, 0, 0, _d->_wx, _d->_wy);

    // Recompute camera for the full window.
    if(_d->_fallback_camera.wmax > 0.f)
        cam_2d_setup(_d->_fallback_spatial.pos.x, _d->_fallback_spatial.pos.y,
                     _d->_fallback_camera.wmax, _d->_fallback_camera.hmax);
}


renderer_service::texture_handle render_simple::create_texture(int w, int h)
{
    return SDL_CreateTexture(_d->_render, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, w, h);
}

void render_simple::update_texture(texture_handle tex, const void* pixels, int pitch)
{
    SDL_UpdateTexture(static_cast<SDL_Texture*>(tex), nullptr, pixels, pitch);
}

void render_simple::destroy_texture(texture_handle tex)
{
    SDL_DestroyTexture(static_cast<SDL_Texture*>(tex));
}

void render_simple::set_clear_color(float r, float g, float b)
{
    _d->_clear_r = r; _d->_clear_g = g; _d->_clear_b = b;
}

void render_simple::on_scene_change()
{
    _d->_clear_r = _d->_clear_g = _d->_clear_b = 0.f;
}

int   render_simple::window_width()  const { return _d->_wx; }
int   render_simple::window_height() const { return _d->_wy; }
float render_simple::display_scale() const { return _d->_scale; }

// RTTI metadata
extern "C" void _rtti_init_render_simple()
{
    entt::meta_factory<nb::render_simple>{}
        .type("render_simple"_hs)
        .custom<rtti::type_info>(rtti::type_info{"render_simple", rtti::TYPE_CLASS_SYSTEM})
        .base<nb::system>()
        .func<&nb::render_simple::cam_2d_setup>("cam_2d_setup"_hs)
        .custom<rtti::func_info>(rtti::func_info{"cam_2d_setup"})
        .func<&nb::render_simple::cam_2d_scale>("cam_2d_scale"_hs)
        .custom<rtti::func_info>(rtti::func_info{"cam_2d_scale"})
        .func<&nb::render_simple::window_width>("window_width"_hs)
        .custom<rtti::func_info>(rtti::func_info{"window_width"})
        .func<&nb::render_simple::window_height>("window_height"_hs)
        .custom<rtti::func_info>(rtti::func_info{"window_height"})
        .func<&nb::render_simple::set_clear_color>("set_clear_color"_hs)
        .custom<rtti::func_info>(rtti::func_info{"set_clear_color"})
        .func<&nb::render_simple::default_viewport>("default_viewport"_hs)
        .custom<rtti::func_info>(rtti::func_info{"default_viewport"})
        .func<&nb::render_simple::display_scale>("display_scale"_hs)
        .custom<rtti::func_info>(rtti::func_info{"display_scale"});
    entt::meta_factory<std::shared_ptr<nb::render_simple>>{rtti::ctx_systems()}
        .type("render_simple_shared"_hs)
        .ctor<&rtti::shared_ptr_builder<nb::render_simple>>()
        .conv<std::shared_ptr<nb::system>>();

    cspatial::_ensure_rtti();
    cstructure::_ensure_rtti();
    csprite::_ensure_rtti();
    cmesh2d::_ensure_rtti();
    cparticle_emitter::_ensure_rtti();
    ccamera::_ensure_rtti();
    clayers::_ensure_rtti();
}
