cmake_minimum_required(VERSION 3.5)
project(newbase)

option(NB_VENDORED "Use vendored libraries" ON)

add_subdirectory(vendored/SDL EXCLUDE_FROM_ALL)
add_subdirectory(vendored/rapidyaml)

add_executable(newbase WIN32
    # entry points
    src/main.c
    src/engine_hooks.cpp

    # engine base
    src/engine.cpp
    src/system_factory.cpp

    # system: render_simple
    src/render_simple/render_simple.cpp

    # system: audio
    src/audio/audio.cpp

    # stb
    src/stb_impl.c

    # Dear ImGui
    vendored/imgui/imgui.cpp
    vendored/imgui/imgui_draw.cpp
    vendored/imgui/imgui_widgets.cpp
    vendored/imgui/imgui_tables.cpp
    vendored/imgui/imgui_demo.cpp  # TODO remove when needed

    vendored/imgui/backends/imgui_impl_sdl3.cpp
    vendored/imgui/backends/imgui_impl_sdlrenderer3.cpp
    vendored/imgui/backends/imgui_impl_sdlgpu3.cpp
)

target_include_directories(newbase PUBLIC include vendored/stb vendored/imgui vendored/imgui)

target_link_libraries(newbase PRIVATE SDL3::SDL3 ryml::ryml -lm)
