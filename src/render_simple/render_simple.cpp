#include <newbase/render_simple/render_simple.hpp>
#include <newbase/engine.hpp>
#include <newbase/scene.hpp>
#include <newbase/components/sprite.hpp>
#include <newbase/components/spatial.hpp>
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
#include "backends/imgui_impl_sdlrenderer3.h"
#include <entt/entt.hpp>
#include <newbase/utility/glm.hpp>
#include <ryml.hpp>
#include <ryml_std.hpp>
#include <tracy/Tracy.hpp>

#include <string>
#include <vector>

using namespace nb;
using entt::operator""_hs;

static SDL_Rect _safe {};
bool _has_ui {false};
float _cam2d_cx {0.0f};
float _cam2d_cy {0.0f};
float _cam2d_wmax {1024.0f};
float _cam2d_hmax {1024.0f};
float _cam2d_scale {1.0f};
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

    Uint32 window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE 
                        | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_MAXIMIZED;
    _win = SDL_CreateWindow(SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING), 1024, 768, window_flags);
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
    SDL_SetWindowPosition(_win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(_win);
    SDL_GetWindowSizeInPixels(_win, &_wx, &_wy);

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

    // register services
    entt::locator<viewport_geometry*>::emplace(this);

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
        // Rendering

        // clear
        SDL_SetRenderDrawColor(_render, 0,0,0,255);
        SDL_RenderClear(_render);

        // cam transform
        glm::mat4x4 viewproj {glm::scale(
            glm::translate(glm::mat4x4{1.0f}, glm::vec3{ _wx/2.0f, _wy/2.0f, 0.0f }), 
            glm::vec3{_cam2d_scale, _cam2d_scale, 1.0f})};

        auto &reg = engine::instance().default_scene().registry(); // TODO change this

        // order spatial components
        reg.sort<cspatial>([](const cspatial &lhs, const cspatial &rhs) {
            return lhs.pos[2] > rhs.pos[2];
        });
        //reg().sort<csprite, cspatial>(); // apply spatial ordering to sprite components 
        
        auto view = reg.view<const cspatial, const csprite>();
        view.use<const cspatial>();
        for(auto [id, spatial, sprite]: view.each()) {
            auto spr_res = sprite.spr;
            auto tex = rman().get_texture(spr_res->id_tex);
            if(!tex->uploaded && tex->surf)
            {
                tex->tex = SDL_CreateTextureFromSurface(_render, tex->surf);
                tex->uploaded = true;
                //if(tex->surf->format == SDL_PIXELFORMAT_XBGR8888)
                //SDL_SetTextureBlendMode(tex->tex, SDL_BLENDMODE_ADD);
                SDL_DestroySurface(tex->surf);
                tex->surf = nullptr;
            }
            if(tex->uploaded)
            {
                glm::vec2 dims = spr_res->dims;
                if(dims == glm::vec2{-1.0f, -1.0f})
                {
                    dims = glm::vec2{tex->tex->w, tex->tex->h};
                }
                auto anchor_delta = dims * spr_res->anchor;

                const glm::vec4 loc_origin{spatial.world * glm::vec4{-anchor_delta.x, -anchor_delta.y, 0.f, 1.f}};
                const glm::vec4 loc_right{spatial.world * glm::vec4{anchor_delta.x, -anchor_delta.y, 0.f, 1.f}};
                const glm::vec4 loc_down{spatial.world * glm::vec4{-anchor_delta.x, anchor_delta.y, 0.f, 1.f}};

                const glm::vec4 view_origin{viewproj * loc_origin};
                const glm::vec4 view_right{viewproj * loc_right};
                const glm::vec4 view_down{viewproj * loc_down}; 
                
                const SDL_FPoint origin{view_origin.x, view_origin.y};
                const SDL_FPoint right{view_right.x, view_right.y};
                const SDL_FPoint down{view_down.x, view_down.y};

                /*std::cerr << _wx << " "<< _wy << std::endl;
                std::cerr << spr_res->anchor.x << " "<< spr_res->anchor.y << std::endl;
                std::cerr << anchor_delta.x << " "<< anchor_delta.y << std::endl;
                std::cerr << loc_origin.x << " "<< loc_origin.y << " "<< loc_origin.z << " " << std::endl;
                std::cerr << glm::to_string(spatial.world) << std::endl;*/
                SDL_RenderTextureAffine(_render, tex->tex, nullptr, &origin, &right, &down);

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
            cam_2d_setup(_cam2d_cx, _cam2d_cy, _cam2d_wmax, _cam2d_hmax);
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

void render_simple::cam_2d_setup(float cx, float cy, float wmax, float hmax)
{
    _cam2d_cx = cx;
    _cam2d_cy = cy;
    _cam2d_wmax = wmax;
    _cam2d_hmax = hmax;

    bool shrink_x = wmax < _wx;
    bool shrink_y = hmax < _wy;

    if(shrink_x)
    {
        if(shrink_y)
        {
            // both
            float scale_x = _wx / wmax;
            float scale_y = _wy / hmax;
            _cam2d_scale = std::min(scale_x, scale_y);
        }
        else
        {
            // just x
            _cam2d_scale = _wx / wmax;
        }
    }
    else
    {
        if(shrink_y)
        {
            // just y
            _cam2d_scale = _wy / hmax;
        }
        else
        {
            // none
            _cam2d_scale = 1.0f;
        }
    }

    log::info("[render_simple] cam2d setup: cx=%f cy=%f wmax=%f hmax=%f wx=%d wy=%d => scale=%f", 
        _cam2d_cx, _cam2d_cy, _cam2d_wmax, _cam2d_hmax, _wx, _wy, _cam2d_scale);
}

float render_simple::cam_2d_scale()
{
    return _cam2d_scale;
}

bool render_simple::get_2d_extents(viewport_geometry::extents_2d &extents)
{
    float span_x = _wx/_cam2d_scale;
    float span_y = _wy/_cam2d_scale;
    extents = {_wx, _wy,
        span_x,
        span_y,
        _cam2d_cx - span_x/2,
        _cam2d_cy - span_y/2,
        _cam2d_cx + span_x/2,
        _cam2d_cy + span_y/2,
        _scale};
    return true;
}


// RTTI metadata
extern "C" void _rtti_init_render_simple()
{
    entt::meta_factory<nb::render_simple>{}
        .type("render_simple"_hs)
        .custom<rtti::type_info>(rtti::type_info{"render_simple", rtti::TYPE_CLASS_SYSTEM})
        .base<nb::system>();
    entt::meta_factory<std::shared_ptr<nb::render_simple>>{rtti::ctx_systems()}
        .type("render_simple_shared"_hs)
        .ctor<&rtti::shared_ptr_builder<nb::render_simple>>()
        .conv<std::shared_ptr<nb::system>>();

    // register related components
    // TODO add component registration mechanism similar to the one used by systems
    cspatial::_ensure_rtti();
    csprite::_ensure_rtti();
}
