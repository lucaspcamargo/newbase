#include <newbase/render_simple/render_simple.h>

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"

#include <string>
#include <vector>


using namespace nb;

render_simple::render_simple():
_win(nullptr)
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[render_simple] constructing");
}

render_simple::~render_simple()
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[render_simple] destroying");
    if(_render)
    {
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();

        SDL_DestroyRenderer(_render);
    }

    if(_win)
        SDL_DestroyWindow(_win);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[render_simple] destroyed");
}


SDL_InitFlags render_simple::sdl_subsystems()
{
    return SDL_INIT_VIDEO;
}


bool render_simple::init(int argc, char **argv)
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[render_simple] init");
    const auto num_drivers = SDL_GetNumRenderDrivers();
    std::vector<std::string> drivers;
    std::string driver_names;
    for(int i = 0; i < num_drivers; i++)
    {
        const auto driver = SDL_GetRenderDriver(i);
        drivers.push_back(driver);
        driver_names += driver;
        driver_names += " ";
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[render_simple] available drivers: %s", driver_names.c_str());

    Uint32 window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
    SDL_Window* _win = SDL_CreateWindow(SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING), 1280, 720, window_flags);
    if (_win == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[render_simple] SDL_CreateWindow(): %s\n", SDL_GetError());
        return false;
    }
    _render = SDL_CreateRenderer(_win, nullptr);
    SDL_SetRenderVSync(_render, 1);
    if (_render == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[render_simple] SDL_CreateRenderer(): %s\n", SDL_GetError());
        return false;
    }
    else
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[render_simple] created renderer: %s", SDL_GetRendererName(_render));
    SDL_SetWindowPosition(_win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(_win);

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
    else if(phase == step_phase::RENDER)
    {
        draw_perf();

        // Rendering
        ImGui::Render();

        SDL_SetRenderDrawColorFloat(_render, 0.0, 0.0, 0.0, 1.0);
        SDL_RenderClear(_render);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), _render);
        SDL_RenderPresent(_render);
    }
    return true;
}

bool render_simple::event(SDL_Event * evt)
{
    ImGui_ImplSDL3_ProcessEvent(evt);
    return true;
}

void render_simple::draw_perf()
{
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
