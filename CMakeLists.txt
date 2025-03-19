cmake_minimum_required(VERSION 3.5)
project(newbase)

set(NEWBASE_VERSION "0.0.0")  # TODO - get from git somehow
option(NEWBASE_TRACING  "Enable tracing with Tracy" OFF)
option(NEWBASE_EMSCRIPTEN_HTML "Ask emcc to compile executables into standalone HTML files too" ON)
set(NEWBASE_EMSCRIPTEN_RES_PREFIX "/nb" CACHE STRING "Where on the emscripten MEMFS to install resources")
set(NEWBASE_NATIVE_RES_PREFIX "./nb" CACHE STRING "Path prefix for installing and searching for resources")

# resolve option configuration
if(DEFINED EMSCRIPTEN)
    if(NEWBASE_EMSCRIPTEN_HTML)
        set(CMAKE_EXECUTABLE_SUFFIX ".html")
    endif()
    set(NEWBASE_DEFAULT_RES_PREFIX "${NEWBASE_EMSCRIPTEN_RES_PREFIX}")
else()
    set(NEWBASE_DEFAULT_RES_PREFIX "${NEWBASE_NATIVE_RES_PREFIX}")
endif()

include(cmake/NewbaseFunctions.cmake)
include(cmake/NewbaseImGui.cmake)
include(cmake/NewbaseLua.cmake)

add_subdirectory(vendored/SDL EXCLUDE_FROM_ALL)
add_subdirectory(vendored/rapidyaml EXCLUDE_FROM_ALL)
set(GLM_BUILD_INSTALL ON CACHE BOOL "GLM forced to add install target for export") # needed for export
add_subdirectory(vendored/glm EXCLUDE_FROM_ALL)
set(ENTT_INSTALL ON) # needed for export
add_subdirectory(vendored/entt EXCLUDE_FROM_ALL)

add_library(newbase STATIC

    # entry points and platform support
    src/sdl/engine_hooks.cpp
    src/sdl/logging_handler.cpp

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

    # system: script_lua
    src/script_lua/script_lua.cpp

    # resource system
    src/res/manager.cpp
    src/res/loaders.cpp

    # yaml conversions
    src/yaml/glm.cpp

    # dependencies: stb and Dear ImGui
    src/stb_impl.c
    ${IMGUI_SOURCES}
)
set_property(TARGET newbase PROPERTY CXX_STANDARD 17)
target_include_directories(newbase PUBLIC include vendored/stb ${IMGUI_INCLUDES} ${LUA_INCLUDES})
target_compile_definitions(newbase PUBLIC -DGLM_ENABLE_EXPERIMENTAL)
target_link_libraries(newbase PUBLIC SDL3::SDL3 ryml::ryml glm::glm EnTT::EnTT lua -lm)

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
