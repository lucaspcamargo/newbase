cmake_minimum_required(VERSION 3.10.0)

project(newbase)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# core vars and options
set(NEWBASE_VERSION "0.0.0")  # TODO - get from git somehow
set(NEWBASE_LOG_LEVEL "DEBUG" CACHE STRING "Minimum log level to compile in. Lower log levels are removed from binary.")
option(NEWBASE_LTO "Enable LTO if available" OFF)
option(NEWBASE_TRACING  "Enable tracing with Tracy" OFF)
option(NEWBASE_SGDK "Enable SGDK compatibility subsystem" ON)
option(NEWBASE_EMSCRIPTEN_HTML "Ask emcc to compile executables into standalone HTML files too" ON)
option(NEWBASE_SDL_STATIC "Build (or use) SDL as a static library" ON)
option(NEWBASE_FDO_FILES "Install freedesktop.org files for the project executables (.desktop and Flatpak manifest)" OFF)
option(NEWBASE_USE_XDG_DATA_DIRS "Use $XDG_DATA_DIRS to locate resources during runtime" OFF)
option(NEWBASE_STEAM "Enable Steamworks subsystem (SDK at vendored/steamworks-sdk)" OFF)
set(NEWBASE_LUA_ROOT "" CACHE STRING "Where to locate lua if downloaded externally (e.g. Flatpak)")
set(NEWBASE_ANDROID_MAIN "" CACHE STRING "Which bin target will be compiled into the main.so for Android")
set(NEWBASE_EMSCRIPTEN_RES_PREFIX "/nb" CACHE STRING "Where on the emscripten MEMFS to install resources")
set(NEWBASE_NATIVE_RES_PREFIX "nb" CACHE STRING "Path prefix for installing and searching for resources")

# options and logging
include(cmake/NewbaseOptions.cmake)
include(cmake/NewbaseLogging.cmake)

# dependencies
include(cmake/NewbaseImGui.cmake)
include(cmake/NewbaseLua.cmake)
include(cmake/NewbaseTracy.cmake)
include(cmake/NewbaseSGDK.cmake)

# core cmake functions (for adding executables, systems, resources, etc)
# should work both when newbase is imported or built directly
include(cmake/NewbaseFunctions.cmake)

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
message("[newbase] adding box2d")
add_subdirectory(vendored/box2d EXCLUDE_FROM_ALL)
if(NOT WIN32 AND NOT NEWBASE_EMSCRIPTEN)
    target_compile_options(box2d PRIVATE -Wno-maybe-uninitialized) # allow warning in b2ShapeDistance
endif()


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
    src/components/script_rtti.cpp
    src/components/spatial_rtti.cpp
    src/components/sprite_rtti.cpp
    src/components/body2d_rtti.cpp

    # resource system
    src/res/manager.cpp
    src/res/loaders.cpp
    src/res/resource.cpp

    # yaml conversions
    src/yaml/glm.cpp

    # rtti
    src/reflection/contexts.cpp

    # utility
    src/utility/imgui_style.cpp

    # optional subsystems
    ${SGDK_SUPPORT_SOURCES}

    # dependencies
    src/stb_impl.c
    ${IMGUI_SOURCES}
    ${TRACY_SOURCES}
)

if(NEWBASE_USE_XDG_DATA_DIRS)
    target_sources(newbase PRIVATE src/utility/xdg.cpp)
endif()

# let engine dependents know where to find core resources
set_target_properties(newbase PROPERTIES
    CORE_RES_DIR $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/res/_nb_core>$<INSTALL_INTERFACE:$<INSTALL_PREFIX>/share/newbase/res/_nb_core>>
)

# CORE SYSTEMS
add_subdirectory(src/render_simple)
add_subdirectory(src/audio)
add_subdirectory(src/input)
add_subdirectory(src/clock)
add_subdirectory(src/script_lua)
add_subdirectory(src/physics2d)
add_subdirectory(src/editor)

# OPTIONAL SYSTEMS
if(NEWBASE_STEAM)
    add_subdirectory(src/steam)
endif()


set_property(TARGET newbase PROPERTY CXX_STANDARD 17)
target_include_directories(newbase PUBLIC include vendored/stb vendored/icon-font-cpp-headers ${IMGUI_INCLUDES} ${LUA_INCLUDES} ${TRACY_INCLUDES})
target_compile_definitions(newbase PUBLIC -DGLM_ENABLE_EXPERIMENTAL ${TRACY_DEFINES})
target_link_libraries(newbase PUBLIC SDL3::SDL3 ryml::ryml glm::glm EnTT::EnTT lua sol2::sol2 box2d::box2d ymfm -lm)

# CONFIG HEADER
configure_file(include/newbase/nb_config.h.in ./include/newbase/nb_config.h @ONLY)
target_include_directories(newbase PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>)


# BINARIES
add_subdirectory(demo)    # our demo game/app
add_subdirectory(runner)  # an application made to run games that don't need additional code
add_executable(dpi_test src/etc/imgui_dpi_test.cpp ${IMGUI_SOURCES})
target_link_libraries(dpi_test SDL3::SDL3)
target_include_directories(dpi_test PRIVATE ${IMGUI_INCLUDES})


# EXPORTS
# let games use newbase and dependencies straight from the build folder
export(EXPORT glm NAMESPACE nb:: FILE newbase-glm-export.cmake) # glm has no export itself, this needs to be done here
export(EXPORT box2dConfig NAMESPACE nb:: FILE newbase-box2d-export.cmake) # same for box2d
export(TARGETS newbase NAMESPACE nb:: FILE newbase-export.cmake )
