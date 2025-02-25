cmake_minimum_required(VERSION 3.5)
project(newbase)

add_subdirectory(vendored/SDL EXCLUDE_FROM_ALL)
add_subdirectory(vendored/rapidyaml EXCLUDE_FROM_ALL)
set(GLM_BUILD_INSTALL ON CACHE BOOL "GLM forced to add install target for export") # needed for export
add_subdirectory(vendored/glm EXCLUDE_FROM_ALL)
set(ENTT_INSTALL ON) # needed for export
add_subdirectory(vendored/entt EXCLUDE_FROM_ALL)

add_library(newbase STATIC

    # entry points
    src/sdl/engine_hooks.cpp

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

target_link_libraries(newbase PUBLIC SDL3::SDL3 ryml::ryml glm::glm EnTT::EnTT -lm)


# BINARIES
add_subdirectory(demo)    # our demo game/app
add_subdirectory(runner)  # an application made to run games that don't need additional code


# EXPORTS
# let games use newbase and dependencies straight from the build folder
export(EXPORT glm FILE glm-export.cmake) # glm has no export itself, this needs to be done here
export(TARGETS newbase NAMESPACE newbase FILE newbase-export.cmake )