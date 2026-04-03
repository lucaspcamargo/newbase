cmake_minimum_required(VERSION 3.13.0)

project(newbase)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# core vars and options
set(NEWBASE_VERSION "0.0.0")  # TODO - get from git somehow
set(NEWBASE_LOG_LEVEL "DEBUG" CACHE STRING "Minimum log level to compile in. Lower log levels are removed from binary.")
option(NEWBASE_LTO "Enable LTO if available" OFF)
option(NEWBASE_TRACING  "Enable tracing with Tracy" OFF)
option(NEWBASE_SGDK "Enable SGDK compatibility subsystem" OFF) # off by default
set(NEWBASE_SGDK_PATH "${CMAKE_SOURCE_DIR}/vendored/SGDK" CACHE PATH "Path to SGDK root")
option(NEWBASE_EMSCRIPTEN_HTML "Ask emcc to compile executables into standalone HTML files too" ON)
option(NEWBASE_SDL_STATIC "Build (or use) SDL as a static library" ON)
option(NEWBASE_FDO_FILES "Install freedesktop.org files for the project executables (.desktop and Flatpak manifest)" OFF)
option(NEWBASE_USE_XDG_DATA_DIRS "Use $XDG_DATA_DIRS to locate resources during runtime" OFF)
option(NEWBASE_STEAM "Enable Steamworks subsystem (SDK at vendored/steamworks-sdk)" OFF)
set(NEWBASE_LUA_ROOT "" CACHE STRING "Where to locate lua if downloaded externally (e.g. Flatpak)")
set(NEWBASE_ANDROID_MAIN "" CACHE STRING "Which bin target will be compiled into the main.so for Android")

# further options and logging
include(cmake/NewbaseOptions.cmake)
include(cmake/NewbaseLogging.cmake)

# python 3 must be available
include(cmake/NewbasePythonInterp.cmake)

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

    # graphplan
    src/graphplan/domain.cpp
    src/graphplan/plan.cpp
    src/graphplan/editor.cpp

    # resource system
    src/res/manager.cpp
    src/res/loaders.cpp
    src/res/resource.cpp
    src/res/storage/sdl_file.cpp
    src/res/storage/sdl_storage.cpp
    src/res/rtti.cpp
    src/res/writers.cpp

    # yaml conversions
    src/yaml/glm.cpp

    # rtti
    src/reflection/contexts.cpp
    src/reflection/coercions.cpp
    src/reflection/lib_glm.cpp
    src/reflection/rtti_dump.cpp
    src/services/rtti_info.cpp

    # ui
    src/ui/manager.cpp
    src/ui/overlay.cpp
    src/ui/imgui_style.cpp

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
add_subdirectory(src/audio)
add_subdirectory(src/clock)
add_subdirectory(src/editor)
add_subdirectory(src/input)
add_subdirectory(src/physics2d)
add_subdirectory(src/render_simple)
add_subdirectory(src/script_lua)
add_subdirectory(src/sgdk)

# OPTIONAL SYSTEMS
if(NEWBASE_STEAM)
    add_subdirectory(src/steam)
endif()


set_property(TARGET newbase PROPERTY CXX_STANDARD 17)
target_include_directories(newbase PUBLIC include vendored/stb vendored/icon-font-cpp-headers ${IMGUI_INCLUDES} ${LUA_INCLUDES} ${TRACY_INCLUDES})
target_compile_definitions(newbase PUBLIC -DGLM_ENABLE_EXPERIMENTAL ${TRACY_DEFINES})
target_compile_options(newbase PUBLIC -fno-exceptions )
target_link_libraries(newbase PUBLIC SDL3::SDL3 ryml::ryml glm::glm EnTT::EnTT lua box2d::box2d -lm)

# CONFIG HEADER
configure_file(include/newbase/nb_config.h.in ./include/newbase/nb_config.h @ONLY)
target_include_directories(newbase PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>)


# BINARIES
add_subdirectory(demo)    # our demo game/app
add_subdirectory(runner)  # an application made to run games that don't need additional code
add_executable(dpi_test src/etc/imgui_dpi_test.cpp ${IMGUI_SOURCES})
target_link_libraries(dpi_test SDL3::SDL3)
target_include_directories(dpi_test PRIVATE ${IMGUI_INCLUDES})

# if we are building on android ndk, adjust SDL linking

if(DEFINED ANDROID_NDK)
    get_target_property( REAL_SDL3 SDL3::SDL3 ALIASED_TARGET)
    message("[newbase] Android: ensuring 16kb page boundary for SDL3 main lib (${REAL_SDL3}) and newbase")
    target_link_options( ${REAL_SDL3} PRIVATE "-Wl,-z,max-page-size=16384")
    target_link_options( newbase PRIVATE "-Wl,-z,max-page-size=16384")
endif()

# EXPORTS
# let games use newbase and dependencies straight from the build folder
export(EXPORT glm NAMESPACE nb:: FILE newbase-glm-export.cmake) # glm has no export itself, this needs to be done here
export(EXPORT box2dConfig NAMESPACE nb:: FILE newbase-box2d-export.cmake) # same for box2d
# iterate over all systems in NEWBASE_ALL_SYSTEMS list and export them
foreach(system IN LISTS NEWBASE_ALL_SYSTEMS)
    set(system_target newbase_sys_${system})
    if(TARGET ${system_target})
        list(APPEND export_targets ${system_target})
    else()
        message(WARNING "System ${system} does not have a target, skipping export")
    endif()
endforeach()
set_target_properties(newbase PROPERTIES ALL_SYSTEMS "${NEWBASE_ALL_SYSTEMS}")
export(TARGETS newbase ${export_targets} NAMESPACE nb:: FILE newbase-export.cmake )

