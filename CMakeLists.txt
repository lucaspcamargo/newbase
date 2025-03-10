cmake_minimum_required(VERSION 3.5)
project(newbase)

set(NEWBASE_VERSION "0.0.0")  # TODO - get from git somehow
option(NEWBASE_TRACING  "Enable tracing with Tracy" OFF)

include(cmake/NewbaseFunctions.cmake)

add_subdirectory(vendored/SDL EXCLUDE_FROM_ALL)
add_subdirectory(vendored/rapidyaml EXCLUDE_FROM_ALL)
set(GLM_BUILD_INSTALL ON CACHE BOOL "GLM forced to add install target for export") # needed for export
add_subdirectory(vendored/glm EXCLUDE_FROM_ALL)
set(ENTT_INSTALL ON) # needed for export
add_subdirectory(vendored/entt EXCLUDE_FROM_ALL)
set(IMGUI_SOURCES
    vendored/imgui/imgui.cpp
    vendored/imgui/imgui_draw.cpp
    vendored/imgui/imgui_widgets.cpp
    vendored/imgui/imgui_tables.cpp
    vendored/imgui/imgui_demo.cpp  # TODO remove when needed
    vendored/imgui/backends/imgui_impl_sdl3.cpp
    vendored/imgui/backends/imgui_impl_sdlrenderer3.cpp
    vendored/imgui/backends/imgui_impl_sdlgpu3.cpp
    )
set(IMGUI_INCLUDES
    vendored/imgui)


add_library(newbase STATIC
    # entry points
    src/sdl/engine_hooks.cpp

    # engine base
    src/engine.cpp
    src/system_factory.cpp
    src/ecs.cpp

    # components
    src/components/builders.cpp

    # system: render_simple
    src/render_simple/render_simple.cpp

    # system: audio
    src/audio/audio.cpp

    # resource system
    src/res/manager.cpp
    src/res/loaders.cpp

    # yaml conversions
    src/yaml/glm.cpp

    # stb
    src/stb_impl.c

    # Dear ImGui
    ${IMGUI_SOURCES}
)
set_property(TARGET newbase PROPERTY CXX_STANDARD 17)
target_include_directories(newbase PUBLIC include vendored/stb ${IMGUI_INCLUDES})
target_compile_definitions(newbase PUBLIC -DGLM_ENABLE_EXPERIMENTAL)
target_link_libraries(newbase PUBLIC SDL3::SDL3 ryml::ryml glm::glm EnTT::EnTT -lm)

# CONFIG HEADER
configure_file(include/newbase/nb_config.h.in ./include/newbase/nb_config.h @ONLY)
target_include_directories(newbase PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/include)


# BINARIES
add_subdirectory(demo)    # our demo game/app
add_subdirectory(runner)  # an application made to run games that don't need additional code
add_executable(dpi_test src/etc/imgui_dpi_test.cpp ${IMGUI_SOURCES})
target_link_libraries(dpi_test SDL3::SDL3)
target_include_directories(dpi_test PRIVATE ${IMGUI_INCLUDES})


# EXPORTS
# let games use newbase and dependencies straight from the build folder
export(EXPORT glm FILE glm-export.cmake) # glm has no export itself, this needs to be done here
export(TARGETS newbase NAMESPACE newbase FILE newbase-export.cmake )