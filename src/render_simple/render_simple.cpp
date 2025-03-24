#include <newbase/render_simple/render_simple.h>
#include <newbase/engine.h>
#include <newbase/scene.h>
#include <newbase/components/sprite.h>
#include <newbase/components/spatial.h>
#include <newbase/res/sprite.h>
#include <newbase/res/texture.h>
#include <newbase/res/manager.h>
#include <newbase/reflection/contexts.h>
#include <newbase/reflection/data.h>
#include <newbase/log.h>

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include <entt/entt.hpp>
#include <glm/gtx/string_cast.hpp>

#include <string>
#include <vector>
#include <iostream> // TODO remove


using namespace nb;
using entt::operator""_hs;

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
    if(_render)
    {
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();

        SDL_DestroyRenderer(_render);
    }

    if(_win)
        SDL_DestroyWindow(_win);

    log::info("[render_simple] destroyed");
}


SDL_InitFlags render_simple::sdl_subsystems()
{
    return SDL_INIT_VIDEO;
}


bool render_simple::init(int argc, char **argv)
{
    log::info("[render_simple] init");
    const auto num_drivers = SDL_GetNumRenderDrivers();
    
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

    Uint32 window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
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
    SDL_GetWindowSize(_win, &_wx, &_wy);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();
    
    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForSDLRenderer(_win, _render);
    ImGui_ImplSDLRenderer3_Init(_render);

    return true;
}

bool render_simple::step(nb::step_phase phase)
{
    if(phase == step_phase::PRE_UPDATE)
    {
        // Start the Dear ImGui frame
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

    }
    else if(phase == step_phase::PRE_RENDER)
    {
        // scale all rendering from logical coordinates, in case of HiDPI 
        if(_scale != 1.0f)
            SDL_SetRenderScale(_render, _scale, _scale);
    }
    else if(phase == step_phase::RENDER)
    {
        // Rendering
        glm::mat4x4 viewproj{glm::translate(glm::mat4x4{1.0f}, glm::vec3{ _wx/2.0f, _wy/2.0f, 0.0f })};
        
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
        draw_perf();
        ImGui::Render();
        
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), _render);
        SDL_RenderPresent(_render);
        
        // reset render scale for HiDPI
        if(_scale != 1.0f)
            SDL_SetRenderScale(_render, 1.0f, 1.0f);
    }
    return true;
}

bool render_simple::event(SDL_Event * evt)
{
    ImGui_ImplSDL3_ProcessEvent(evt);

    if(evt->type == SDL_EVENT_WINDOW_RESIZED)
    {
        if(evt->window.windowID == SDL_GetWindowID(_win))
        {
            _wx = evt->window.data1;
            _wy = evt->window.data2;
            std::cerr << "Resized to " << _wx << "x" << _wy << std::endl;
        }
    }

    return true;
}

void render_simple::draw_perf()
{

    //ImGui::ShowDemoWindow(nullptr);
    // TODO make proper perfcounters
    static int location = 3;
    ImGuiIO& io = ImGui::GetIO();
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (location >= 0)
    {
        const float PAD = 10.0f;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 work_pos = viewport->WorkPos; // Use work area to avoid menu-bar/task-bar, if any!
        ImVec2 work_size = viewport->WorkSize;
        ImVec2 window_pos, window_pos_pivot;
        window_pos.x = (location & 1) ? (work_pos.x + work_size.x - PAD) : (work_pos.x + PAD);
        window_pos.y = (location & 2) ? (work_pos.y + work_size.y - PAD) : (work_pos.y + PAD);
        window_pos_pivot.x = (location & 1) ? 1.0f : 0.0f;
        window_pos_pivot.y = (location & 2) ? 1.0f : 0.0f;
        ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
        window_flags |= ImGuiWindowFlags_NoMove;
    }
    else if (location == -2)
    {
        // Center window
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        window_flags |= ImGuiWindowFlags_NoMove;
    }
    ImGui::SetNextWindowBgAlpha(0.5f); // Transparent background
    if (ImGui::Begin("fps overlay", nullptr, window_flags))
    {
        ImGui::Text("%s %s", SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING),
                    SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_VERSION_STRING));
        ImGui::Separator();
        ImGui::Text("FPS: %.1f", io.Framerate);
        if (ImGui::BeginPopupContextWindow())
        {
            if (ImGui::MenuItem("Custom",       NULL, location == -1)) location = -1;
            if (ImGui::MenuItem("Center",       NULL, location == -2)) location = -2;
            if (ImGui::MenuItem("Top-left",     NULL, location == 0)) location = 0;
            if (ImGui::MenuItem("Top-right",    NULL, location == 1)) location = 1;
            if (ImGui::MenuItem("Bottom-left",  NULL, location == 2)) location = 2;
            if (ImGui::MenuItem("Bottom-right", NULL, location == 3)) location = 3;
            ImGui::EndPopup();
        }
    }
    ImGui::End();
}



// RTTI metadata

[[nodiscard]] static bool _rtti_register()
{
    entt::meta_factory<nb::render_simple>{rtti::ctx_systems()}
        .type("render_simple"_hs)
        .custom<rtti::cstr>("render_simple")
        .base<nb::system>();
    entt::meta_factory<std::shared_ptr<nb::render_simple>>{rtti::ctx_systems()}
        .type("render_simple_shared"_hs)
        .ctor<&rtti::shared_ptr_builder<nb::render_simple>>()
        .conv<std::shared_ptr<nb::system>>();
    return true;
}

static bool _rtti_registered_render_simple = _rtti_register();