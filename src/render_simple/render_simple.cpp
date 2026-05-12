#include <newbase/render_simple/render_simple.hpp>
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
#include "imgui_internal.h" // for ImGuiViewport
#include "backends/imgui_impl_sdl3.h"
#include "../ui/imgui_impl_sdlrenderer3.h"
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

static SDL_Rect _safe {};
static std::vector<SDL_Vertex> _xform_buf {};
bool _has_ui {false};
#ifdef TRACY_ENABLE
static SDL_Surface *_tracyCopy {nullptr};
#endif


render_simple::render_simple():
_win(nullptr),
_render(nullptr),
_scale(1.0),
_wx(0), _wy(0)
{
    log::info("[render_simple] constructed");
    
    // register services
    entt::locator<renderer_service*>::emplace(this);
}

render_simple::~render_simple()
{
    log::info("[render_simple] destroying");
    if(_has_ui)
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
    if(_render)
    {
        SDL_DestroyRenderer(_render);
    }

    if(_win)
        SDL_DestroyWindow(_win);

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
    
    /* Listing available drivers: disabled because it may be slow
    
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
    */

#ifdef NEWBASE_WII
    Uint32 window_flags = 0;
    int window_w = 640, window_h = 480;
#else
    Uint32 window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
                        | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_MAXIMIZED;
    int window_w = 1024, window_h = 768;
#endif
    _win = SDL_CreateWindow(SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING), window_w, window_h, window_flags);
    if (_win == nullptr)
    {
        log::error("[render_simple] SDL_CreateWindow(): %s\n", SDL_GetError());
        return false;
    }
    _scale = SDL_GetWindowDisplayScale(_win);
    log::info("[render_simple] window scale: %f", _scale);

    _render = SDL_CreateRenderer(_win, nullptr);
    log::info("[render_simple] renderer: %s", SDL_GetRendererName(_render));
    
    SDL_SetRenderVSync(_render, 1);
    if (_render == nullptr)
    {
        log::error("[render_simple] SDL_CreateRenderer(): %s\n", SDL_GetError());
        return false;
    }
    else
        log::info("[render_simple] created renderer: %s", SDL_GetRendererName(_render));

#ifndef NEWBASE_WII
    SDL_SetWindowPosition(_win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
#endif

    SDL_ShowWindow(_win);
    SDL_GetWindowSizeInPixels(_win, &_wx, &_wy);

    // attempt to load and set window icon
    auto icon_tex = rman().get<rtexture>("_nb_core/icon_192.png"_hs);
    if(icon_tex && icon_tex->surf)
    {
        SDL_SetWindowIcon(_win, icon_tex->surf);
    }

    // init gui via ui manager
    ui_manager* ui_mgr = entt::locator<ui_manager*>::value();
    if(ui_mgr)
    {
        _has_ui = ui_mgr->ui_init();
    }
    else
        log::warn("[render_simple] could not init ui via ui_manager service");
    
    if(_has_ui)
    {
        // Setup Platform/Renderer backends
        ImGui_ImplSDL3_InitForSDLRenderer(_win, _render);
        ImGui_ImplSDLRenderer3_Init(_render);

        ui_mgr->ui_init_finish(_scale);
    }
    else
    {
        log::warn("[render_simple] no ui, not initializing ImGui renderer");
    }


    SDL_GetWindowSafeArea(_win, &_safe);
    log::info("[render_simple] safe area: %dx%d@%d,%d", _safe.w, _safe.h, _safe.x, _safe.y);

    // Create the persistent default viewport (full window, no clear).
    _default_vp = create_viewport(0, 0, _wx, _wy, false);
    log::info("[render_simple] default viewport: %u", _default_vp);

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
            ui_mgr->ui_new_frame(_safe.x, _safe.y, _safe.w, _safe.h);
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

        // Full-screen clear first
        SDL_SetRenderDrawColor(_render,
            static_cast<Uint8>(_clear_r * 255), static_cast<Uint8>(_clear_g * 255),
            static_cast<Uint8>(_clear_b * 255), 255);
        SDL_RenderClear(_render);

        const auto &layers = engine::instance().render_layers();

        if(layers.empty())
        {
            log::verb("[render] no render layers, using fallback path");
            // Fallback: draw default scene through the default viewport.
            auto dvp_it = _viewports.find(_default_vp);
            const viewport_entry &dvp = (dvp_it != _viewports.end())
                ? dvp_it->second
                : viewport_entry{ 0, 0, _wx, _wy, false, 0, 0, 0, 1 };

            float cam_cx = _fallback_spatial.pos.x;
            float cam_cy = _fallback_spatial.pos.y;
            float zoom   = _fallback_camera.zoom > 0.f ? _fallback_camera.zoom : 1.f;
            float vp_cx  = dvp.x + dvp.w * 0.5f;
            float vp_cy  = dvp.y + dvp.h * 0.5f;
            glm::mat4x4 viewproj =
                glm::translate(glm::mat4x4{1.0f}, glm::vec3{vp_cx, vp_cy, 0.f}) *
                glm::scale(glm::mat4x4{1.0f}, glm::vec3{zoom, zoom, 1.f}) *
                glm::translate(glm::mat4x4{1.0f}, glm::vec3{-cam_cx, -cam_cy, 0.f});
            auto &reg = engine::instance().default_scene().registry();
            SDL_Rect clip_rect { dvp.x, dvp.y, dvp.w, dvp.h };
            SDL_SetRenderClipRect(_render, &clip_rect);
            _draw_scene(reg, viewproj, 0xFFFFFFFF, dvp);
            SDL_SetRenderClipRect(_render, nullptr);
        }
        else
        {
            for(const auto &layer : layers)
            {
                auto *sc = engine::instance().find_scene(layer.scene_id);
                if(!sc) continue;

                auto it = _viewports.find(layer.viewport);
                if(it == _viewports.end()) continue;
                const auto &vp = it->second;

                // Clear viewport region if requested
                if(vp.clear)
                {
                    SDL_SetRenderDrawColor(_render,
                        static_cast<Uint8>(vp.r * 255), static_cast<Uint8>(vp.g * 255),
                        static_cast<Uint8>(vp.b * 255), static_cast<Uint8>(vp.a * 255));
                    SDL_FRect clip { static_cast<float>(vp.x), static_cast<float>(vp.y),
                                     static_cast<float>(vp.w), static_cast<float>(vp.h) };
                    SDL_RenderFillRect(_render, &clip);
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
                SDL_SetRenderClipRect(_render, &clip_rect);
                _draw_scene(sc->registry(), viewproj, layer.layer_mask, vp);
                SDL_SetRenderClipRect(_render, nullptr);
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
        if(_scale != 1.0f)
            SDL_SetRenderScale(_render, _scale, _scale);
#endif
        // fixed overlays
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), _render);
        // reset render scale for HiDPI
#ifndef ANDROID
        if(_scale != 1.0f)
            SDL_SetRenderScale(_render, 1.0f, 1.0f);
#endif

#ifdef TRACY_ENABLED
        // TODO
        // rebuild _tracyCopy to be able to hold current framebuffer (if needed)
        // Call SDL_RenderReadPixels() to fill buffer
        // Send buffer to tracy profiler
#endif
        SDL_RenderPresent(_render);
        FrameMark;
    }
    return true;
}

bool render_simple::event( SDL_Event * evt)
{
    ImGui_ImplSDL3_ProcessEvent(evt);

    if(evt->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
    {
        if(evt->window.windowID == SDL_GetWindowID(_win))
        {
            _wx = evt->window.data1;
            _wy = evt->window.data2;
            if(!_default_vp_owned && _default_vp != VIEWPORT_INVALID)
                update_viewport(_default_vp, 0, 0, _wx, _wy);
            if(_fallback_camera.wmax > 0.f)
                cam_2d_setup(_fallback_spatial.pos.x, _fallback_spatial.pos.y,
                             _fallback_camera.wmax, _fallback_camera.hmax);
            log::info("[render_simple] resized to %dx%d", _wx, _wy);
        }
    }
    else if(evt->type == SDL_EVENT_WINDOW_RESIZED)
    {
        if(evt->window.windowID == SDL_GetWindowID(_win))
        {
            SDL_GetWindowSafeArea(_win, &_safe);
            log::info("[render_simple] safe area: %dx%d@%d,%d", _safe.w, _safe.h, _safe.x, _safe.y);
        }
    }

    if(evt->type == SDL_EVENT_KEY_DOWN && evt->key.scancode == SDL_SCANCODE_F11)
        SDL_SetWindowFullscreen(_win, !(SDL_GetWindowFlags(_win)&SDL_WINDOW_FULLSCREEN));

    return true;
}


int render_simple::window_width()
{
    return _wx;
}

int render_simple::window_height()
{
    return _wy;
}

void render_simple::_draw_scene(entt::registry &reg, const glm::mat4x4 &viewproj,
                                uint32_t layer_mask, const viewport_entry &/*vp*/)
{
    reg.sort<cspatial>([](const cspatial &lhs, const cspatial &rhs) {
        return lhs.pos[2] > rhs.pos[2];
    });

    auto spatial_view = reg.view<const cspatial>();
    for (auto [id, spatial] : spatial_view.each())
    {
        if (auto* lyr = reg.try_get<clayers>(id); lyr && !(lyr->mask & layer_mask))
            continue;

        if (auto* sprite = reg.try_get<const csprite>(id))
        {
            if (!sprite->visible) continue;
            auto spr_res = sprite->spr;
            if (!spr_res) continue;
            auto tex = spr_res->tex;
            if (!tex) continue;
            if (!tex->uploaded && tex->surf)
            {
                tex->tex = SDL_CreateTextureFromSurface(_render, tex->surf);
                SDL_SetTextureScaleMode(tex->tex, SDL_SCALEMODE_NEAREST);
                tex->uploaded = true;
                SDL_DestroySurface(tex->surf);
                tex->surf = nullptr;
            }
            if (!tex->uploaded) continue;

            const glm::vec4& csr = sprite->current_source_rect;
            SDL_FRect src_rect_val;
            const SDL_FRect* src_rect = nullptr;
            if (csr.z > 0.f)
            {
                src_rect_val = { csr.x, csr.y, csr.z, csr.w };
                src_rect = &src_rect_val;
            }

            glm::vec2 dims = spr_res->dims;
            if (dims == glm::vec2{-1.0f, -1.0f})
                dims = src_rect ? glm::vec2{csr.z, csr.w} : glm::vec2{tex->tex->w, tex->tex->h};

            auto anchor_delta = dims * spr_res->anchor;
            const glm::vec4 loc_origin{spatial.world * glm::vec4{-anchor_delta.x, -anchor_delta.y, 0.f, 1.f}};
            const glm::vec4 loc_right {spatial.world * glm::vec4{ anchor_delta.x, -anchor_delta.y, 0.f, 1.f}};
            const glm::vec4 loc_down  {spatial.world * glm::vec4{-anchor_delta.x,  anchor_delta.y, 0.f, 1.f}};

            auto snap = [&](float v) { return sprite->pixel_snap ? std::roundf(v) : v; };
            const glm::vec4 vp_origin = viewproj * loc_origin;
            const glm::vec4 vp_right  = viewproj * loc_right;
            const glm::vec4 vp_down   = viewproj * loc_down;
            const SDL_FPoint origin{snap(vp_origin.x), snap(vp_origin.y)};
            const SDL_FPoint right {snap(vp_right .x), snap(vp_right .y)};
            const SDL_FPoint down  {snap(vp_down  .x), snap(vp_down  .y)};

            SDL_SetTextureColorModFloat(tex->tex, sprite->color.r, sprite->color.g, sprite->color.b);
            SDL_SetTextureAlphaModFloat(tex->tex, sprite->color.a);
            SDL_RenderTextureAffine(_render, tex->tex, src_rect, &origin, &right, &down);
            SDL_SetTextureColorModFloat(tex->tex, 1.f, 1.f, 1.f);
            SDL_SetTextureAlphaModFloat(tex->tex, 1.f);
        }
        else if (auto* mesh = reg.try_get<const cmesh2d>(id))
        {
            if (!mesh->visible) continue;
            if (!mesh->geom || mesh->geom->empty()) continue;

            SDL_Texture* sdl_tex = nullptr;
            if (mesh->tex)
            {
                auto* tex = mesh->tex.get();
                if (!tex->uploaded && tex->surf)
                {
                    tex->tex = SDL_CreateTextureFromSurface(_render, tex->surf);
                    SDL_SetTextureScaleMode(tex->tex, SDL_SCALEMODE_NEAREST);
                    tex->uploaded = true;
                    SDL_DestroySurface(tex->surf);
                    tex->surf = nullptr;
                }
                if (tex->uploaded) sdl_tex = tex->tex;
            }

            const auto& src = mesh->geom->vertices;
            _xform_buf.resize(src.size());
            for (size_t i = 0; i < src.size(); ++i)
            {
                const auto& v = src[i];
                const glm::vec4 wp = viewproj * (spatial.world * glm::vec4(v.pos.x, v.pos.y, 0.f, 1.f));
                _xform_buf[i] = SDL_Vertex{
                    .position  = {mesh->pixel_snap ? std::roundf(wp.x) : wp.x,
                                  mesh->pixel_snap ? std::roundf(wp.y) : wp.y},
                    .color     = {v.color.r, v.color.g, v.color.b, v.color.a},
                    .tex_coord = {v.uv.x, v.uv.y},
                };
            }

            const SDL_BlendMode sdl_bm = (mesh->blend_mode == blend_mode_2d::ADD)
                ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND;
            if (sdl_tex) SDL_SetTextureBlendMode(sdl_tex, sdl_bm);
            SDL_SetRenderDrawBlendMode(_render, sdl_bm);

            const auto& idx = mesh->geom->indices;
            SDL_RenderGeometry(_render, sdl_tex,
                               _xform_buf.data(), static_cast<int>(_xform_buf.size()),
                               idx.empty() ? nullptr : idx.data(),
                               static_cast<int>(idx.size()));

            SDL_SetRenderDrawBlendMode(_render, SDL_BLENDMODE_BLEND);
        }
    }
}

void render_simple::cam_2d_setup(float cx, float cy, float wmax, float hmax)
{
    _fallback_spatial.pos = { cx, cy, 0.f };
    _fallback_camera.wmax = wmax;
    _fallback_camera.hmax = hmax;

    float scale_x = _wx / wmax;
    float scale_y = _wy / hmax;
    _fallback_camera.zoom = std::min(scale_x, scale_y);

    log::verb("[render_simple] cam2d setup: cx=%f cy=%f wmax=%f hmax=%f => zoom=%f",
        cx, cy, wmax, hmax, _fallback_camera.zoom);
}

float render_simple::cam_2d_scale()
{
    return _fallback_camera.zoom;
}

bool render_simple::get_2d_extents(renderer_service::extents_2d &extents)
{
    // Prefer the first configured render layer's camera
    const auto &layers = engine::instance().render_layers();
    if(!layers.empty())
    {
        const auto &layer = layers.front();
        auto *sc = engine::instance().find_scene(layer.scene_id);
        auto it  = _viewports.find(layer.viewport);
        if(sc && it != _viewports.end())
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
            extents = { vp.w, vp.h, span_x, span_y,
                cx - span_x * 0.5f, cy - span_y * 0.5f,
                cx + span_x * 0.5f, cy + span_y * 0.5f,
                _scale, vp.x, vp.y };
            return true;
        }
    }

    // Fallback: use the default viewport's current rect.
    auto dvp_it = _viewports.find(_default_vp);
    int dvp_w = (dvp_it != _viewports.end()) ? dvp_it->second.w : _wx;
    int dvp_h = (dvp_it != _viewports.end()) ? dvp_it->second.h : _wy;
    float zoom   = _fallback_camera.zoom > 0.f ? _fallback_camera.zoom : 1.f;
    float cx     = _fallback_spatial.pos.x;
    float cy     = _fallback_spatial.pos.y;
    float span_x = dvp_w / zoom;
    float span_y = dvp_h / zoom;
    int dvp_x = (dvp_it != _viewports.end()) ? dvp_it->second.x : 0;
    int dvp_y = (dvp_it != _viewports.end()) ? dvp_it->second.y : 0;
    extents = { dvp_w, dvp_h, span_x, span_y,
        cx - span_x * 0.5f, cy - span_y * 0.5f,
        cx + span_x * 0.5f, cy + span_y * 0.5f,
        _scale, dvp_x, dvp_y };
    return true;
}

viewport_handle render_simple::create_viewport(int x, int y, int w, int h,
                                               bool clear, float r, float g, float b, float a)
{
    viewport_handle handle = _next_vp_handle++;
    _viewports[handle] = { x, y, w, h, clear, r, g, b, a };
    log::info("[render_simple] viewport %u created: %dx%d@%d,%d", handle, w, h, x, y);
    return handle;
}

void render_simple::update_viewport(viewport_handle vp, int x, int y, int w, int h)
{
    auto it = _viewports.find(vp);
    if(it == _viewports.end()) return;
    it->second.x = x; it->second.y = y;
    it->second.w = w; it->second.h = h;

    if(vp == _default_vp)
    {
        _default_vp_owned = true;
        // Recompute zoom to fit the new viewport dimensions.
        if(_fallback_camera.wmax > 0.f && w > 0 && h > 0)
        {
            _fallback_camera.zoom = std::min(
                float(w) / _fallback_camera.wmax,
                float(h) / _fallback_camera.hmax);
        }
    }
}

void render_simple::destroy_viewport(viewport_handle vp)
{
    _viewports.erase(vp);
}

viewport_handle render_simple::default_viewport() const
{
    return _default_vp;
}

void render_simple::reset_default_viewport()
{
    _default_vp_owned = false;
    if(_default_vp != VIEWPORT_INVALID)
        update_viewport(_default_vp, 0, 0, _wx, _wy);
    // Recompute zoom for the full window.
    if(_fallback_camera.wmax > 0.f)
        cam_2d_setup(_fallback_spatial.pos.x, _fallback_spatial.pos.y,
                     _fallback_camera.wmax, _fallback_camera.hmax);
}


renderer_service::texture_handle render_simple::create_texture(int w, int h)
{
    return SDL_CreateTexture(_render, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, w, h);
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
    _clear_r = r; _clear_g = g; _clear_b = b;
}

void render_simple::on_scene_change()
{
    _clear_r = _clear_g = _clear_b = 0.f;
}

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
