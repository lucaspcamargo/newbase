cmake_minimum_required(VERSION 3.9.0)

project(newbase)

set(NEWBASE_VERSION "0.0.0")  # TODO - get from git somehow
set(NEWBASE_LOG_LEVEL "DEBUG" CACHE STRING "Minimum log level to compile in. Lower log levels are removed from binary.")
option(NEWBASE_LTO "Enable LTO if available" OFF)
option(NEWBASE_TRACING  "Enable tracing with Tracy" OFF)
option(NEWBASE_EMSCRIPTEN_HTML "Ask emcc to compile executables into standalone HTML files too" ON)
option(NEWBASE_SDL_STATIC "Build (or use) SDL as a static library" ON)
option(NEWBASE_FDO_FILES "Install freedesktop.org files for the project executables (.desktop and Flatpak manifest)" OFF)
set(NEWBASE_LUA_ROOT "" CACHE STRING "Where to locate lua if downloaded externally (e.g. Flatpak)")
set(NEWBASE_ANDROID_MAIN "" CACHE STRING "Which bin target will be compiled into the main.so for Android")
set(NEWBASE_EMSCRIPTEN_RES_PREFIX "/nb" CACHE STRING "Where on the emscripten MEMFS to install resources")
set(NEWBASE_NATIVE_RES_PREFIX "nb" CACHE STRING "Path prefix for installing and searching for resources")

include(cmake/NewbaseOptions.cmake)
include(cmake/NewbaseFunctions.cmake)
include(cmake/NewbaseLogging.cmake)
include(cmake/NewbaseImGui.cmake)
include(cmake/NewbaseLua.cmake)

message("[newbase] adding SDL")
add_subdirectory(vendored/SDL EXCLUDE_FROM_ALL)
message("[newbase] adding rapidyaml")
add_subdirectory(vendored/rapidyaml EXCLUDE_FROM_ALL)
message("[newbase] adding sol2")
add_subdirectory(vendored/sol2 EXCLUDE_FROM_ALL)
message("[newbase] adding glm")
set(GLM_BUILD_INSTALL ON CACHE BOOL "GLM forced to add install target for export") # needed for export
add_subdirectory(vendored/glm EXCLUDE_FROM_ALL)
message("[newbase] adding entt")
set(ENTT_INSTALL ON) # needed for export
add_subdirectory(vendored/entt EXCLUDE_FROM_ALL)

add_library(newbase STATIC

    # entry points and platform support
    src/sdl/engine_hooks.cpp
    src/sdl/logging_handler.cpp

    # engine base
    src/engine.cpp
    src/scene.cpp
    src/system_factory.cpp

    # components
    src/components/builders.cpp
    src/components/spatial_rtti.cpp
    src/components/sprite_rtti.cpp

    # system: render_simple
    src/render_simple/render_simple.cpp

    # system: audio
    src/audio/audio.cpp
    src/audio/lua_bindings.cpp

    # system: script_lua
    src/script_lua/script_lua.cpp
    src/script_lua/bindings_glm.cpp

    # system: editor
    src/editor/editor.cpp

    # resource system
    src/res/manager.cpp
    src/res/loaders.cpp

    # yaml conversions
    src/yaml/glm.cpp

    # rtti
    src/reflection/contexts.cpp

    # dependencies: stb and Dear ImGui
    src/stb_impl.c
    ${IMGUI_SOURCES}
)
set_property(TARGET newbase PROPERTY CXX_STANDARD 17)
target_include_directories(newbase PUBLIC include vendored/stb vendored/icon-font-cpp-headers ${IMGUI_INCLUDES} ${LUA_INCLUDES})
target_compile_definitions(newbase PUBLIC -DGLM_ENABLE_EXPERIMENTAL)
target_link_libraries(newbase PUBLIC SDL3::SDL3 ryml::ryml glm::glm EnTT::EnTT lua sol2::sol2 -lm)

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
