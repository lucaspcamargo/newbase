#include <newbase/render_simple/render_simple.h>

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"


using namespace nb;

render_simple::render_simple():
_win(nullptr)
{
}

render_simple::~render_simple()
{
    if(_win)
        SDL_DestroyWindow(_win);
}


SDL_InitFlags render_simple::sdl_subsystems()
{
    return SDL_INIT_VIDEO;
}


bool render_simple::init(int argc, char **argv)
{
    Uint32 window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |  SDL_WINDOW_HIDDEN;
    SDL_Window* _win = SDL_CreateWindow("Dear ImGui SDL3+SDL_Renderer example", 1280, 720, window_flags);
    if (_win == nullptr)
    {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return false;
    }
    _render = SDL_CreateRenderer(_win, nullptr);
    SDL_SetRenderVSync(_render, 1);
    if (_render == nullptr)
    {
        SDL_Log("Error: SDL_CreateRenderer(): %s\n", SDL_GetError());
        return false;
    }
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
    if(phase == step_phase::GENERAL_UPDATE)
    {
        // Start the Dear ImGui frame
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        static bool open = true;
        ImGui::ShowDemoWindow(&open);
    }
    else if(phase == step_phase::RENDER)
    {

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
