if(DEFINED ANDROID_NDK)
    # clang is very annoying
    add_compile_options(
            -Wno-format-nonliteral # for log wrappers
            -Wno-format-security   # ditto
    )
endif()

if(NEWBASE_LTO)
    message("[newbase] LTO requested, checking support...")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT newbase_ipo_supported OUTPUT error)
    if(newbase_ipo_supported)
        message("[newbase] LTO is available, enabled by default for all targets")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
        set(NEWBASE_LTO_ENABLED TRUE)
    else()
        message("[newbase] LTO is not available, skipping setup")
        set(NEWBASE_LTO_ENABLED FALSE)
    endif()
endif()

# resolve option configuration
if(DEFINED EMSCRIPTEN)
    set(NEWBASE_EMSCRIPTEN ON)
    if(NEWBASE_EMSCRIPTEN_HTML)
        set(CMAKE_EXECUTABLE_SUFFIX ".html")
    endif()
    set(NEWBASE_DEFAULT_RES_PREFIX "${NEWBASE_EMSCRIPTEN_RES_PREFIX}")
else()
    set(NEWBASE_EMSCRIPTEN OFF)
    set(NEWBASE_DEFAULT_RES_PREFIX "${NEWBASE_NATIVE_RES_PREFIX}")
endif()

if(NEWBASE_SDL_STATIC)
    set(SDL_STATIC ON)
    set(SDL_SHARED OFF)
else()
    set(SDL_STATIC OFF)
    set(SDL_SHARED ON)
endif()
