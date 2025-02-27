#include <iostream>
#include <SDL3/SDL.h>
#include <imgui.h>
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"

int WINDOW_WIDTH_REQESTED = 800;
int WINDOW_HEIGHT_REQUESTED = 600;

int main(int argc, char* args[]) {
  int isHidpi = 0;
  for (int i = 1; i < argc; i++) {
      if (strcmp(args[i], "--hidpiA") == 0) {
          isHidpi = 1;
      } else if (strcmp(args[i], "--hidpiB") == 0) {
          isHidpi = 2;
      }
  }

  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
    return 1;
  }

  SDL_Window *window = nullptr;
  if (isHidpi != 0) {
    window = SDL_CreateWindow("SDL3 App", WINDOW_WIDTH_REQESTED, WINDOW_HEIGHT_REQUESTED, SDL_WINDOW_HIGH_PIXEL_DENSITY);
    std::cout << "SDL_Window set: SDL_WINDOW_HIGH_PIXEL_DENSITY" << std::endl;
  } else {
    window = SDL_CreateWindow("SDL3 App", WINDOW_WIDTH_REQESTED, WINDOW_HEIGHT_REQUESTED, 0);
  }
  if (!window) {
    std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << std::endl;
    return 1;
  }

  SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);
  if (!renderer) {
    std::cerr << "Renderer could not be created! SDL_Error: " << SDL_GetError() << std::endl;
    return 1;
  }
  std::cout << "Renderer Name: " << SDL_GetRendererName(renderer) << std::endl;

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();

  // Setup Platform/Renderer backends
  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer3_Init(renderer);


  ImGuiIO& io = ImGui::GetIO(); (void)io;
  float displayScale = SDL_GetWindowDisplayScale(window);
  int pixWidth, pixHeight, winWidth, winHeight;
  SDL_GetWindowSizeInPixels(window, &pixWidth, &pixHeight);
  SDL_GetWindowSize(window, &winWidth, &winHeight);

  if (isHidpi) {
    if (isHidpi == 1) {
      io.DisplaySize = ImVec2(static_cast<float>(pixWidth), static_cast<float>(pixHeight));
      std::cout << "ImGUI DisplaySize set pixels" << std::endl;
    } else if (isHidpi == 2) {
      io.DisplaySize = ImVec2(static_cast<float>(winWidth), static_cast<float>(winHeight));
      std::cout << "ImGUI DisplaySize set points" << std::endl;
    }
    io.DisplayFramebufferScale = ImVec2(displayScale, displayScale);
    //io.FontGlobalScale = displayScale;
    //ImGui_ImplSDLRenderer3_DestroyDeviceObjects();
    //ImGui_ImplSDLRenderer3_CreateDeviceObjects();

    //ImGui::GetStyle().ScaleAllSizes(displayScale);
  } else {
    io.DisplaySize = ImVec2(static_cast<float>(pixWidth), static_cast<float>(pixHeight));
  }

  std::cout << "SDL Window Scale: " << displayScale << std::endl;
  std::cout << "SDL Window Point Size: " << winWidth << "x" << winHeight << std::endl;
  std::cout << "SDL Window Pixel Size: " << pixWidth << "x" << pixHeight << std::endl;
  std::cout << "ImGui Display Size: " << io.DisplaySize.x << "x" << io.DisplaySize.y << std::endl;
  std::cout << "ImGui Display Scale: " << io.DisplayFramebufferScale.x << "x" << io.DisplayFramebufferScale.y << std::endl;

  bool appRunning = true;

  while (appRunning) {
    SDL_Event e;
    ImGuiIO& newIo = ImGui::GetIO(); (void)newIo;

    while (SDL_PollEvent(&e) != 0) {
      ImGui_ImplSDL3_ProcessEvent(&e);
      switch (e.type) {
        case SDL_EVENT_QUIT:
          appRunning = false;
          break;

        case SDL_EVENT_KEY_DOWN:
          if (e.key.key == SDLK_SPACE) {
            std::cout << "Running ImGui Display Size: " << newIo.DisplaySize.x << "x" << newIo.DisplaySize.y << std::endl;
            std::cout << "Running ImGui Display Scale: " << newIo.DisplayFramebufferScale.x << "x" << newIo.DisplayFramebufferScale.y << std::endl;
          }
          break;

        default:
          break;
      }
    }

    // Clear the screen
    SDL_SetRenderDrawColorFloat(renderer, 0.45f, 0.55f, 0.60f, 1.00f);
    SDL_RenderClear(renderer);
  
    // New frame
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // Draw GUI
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(io.DisplaySize.x, io.DisplaySize.y), IM_COL32(0, 0, 255, 255));
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(100, 100), ImVec2(200, 200), IM_COL32(0, 255, 0, 255));

    static bool demoshow = true;
    ImGui::ShowDemoWindow(&demoshow);

    // Display frame
    ImGui::Render();
    SDL_SetRenderScale(renderer, displayScale, displayScale);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
    SDL_RenderPresent(renderer);
  }

  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();

  if (renderer) {
    SDL_DestroyRenderer(renderer);
  }
  if (window) {
    SDL_DestroyWindow(window);
  }

  SDL_Quit();

  return 0;
}
