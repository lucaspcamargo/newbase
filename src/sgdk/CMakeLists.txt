# add SGDK system

if(NEWBASE_SGDK)

    # Collect translated SGDK sources produced at configure time by sgdk_translator.py
    file(GLOB_RECURSE SGDK_TRANSLATED_SOURCES
        "${SGDK_TRANSLATED_DIR}/src/*.c"
    )
    if(NOT SGDK_TRANSLATED_SOURCES)
        message(WARNING "[sgdk] no translated SGDK sources found in ${SGDK_TRANSLATED_DIR}/src — was sgdk_translator.py run?")
    endif()
    

    # SGDK source files replaced entirely by the PAL layer — must not be compiled.
    # Add filenames (no path) here as new hardware subsystems are shimmed.
    set(SGDK_PAL_REPLACED
        /sys.c$         # threading / init  → sgdk.cpp
        /dma.c$         # DMA engine        → e_vdp.cpp (write_ctrl/data)
        /joy.c$         # input             → newbase input system
        /z80_ctrl.c$    # Z80 bus           → api.cpp stubs
        /ym2612.c$      # FM synthesis      → ymfm
        /psg.c$         # PSG synthesis     → e_psg (TODO)
        /memory.c$      # MD memory map     → not applicable on PC
        /mapper.c$      # cartridge mapper  → not applicable on PC
        /sram.c$        # save RAM          → not applicable on PC
        /timer.c$       # hardware timer    → SDL timer
        /bmp.c$         # framebuffer functionality -> not implemented, too asm-heavy

        ## sound implementations
        /snd/

        ## DISCARDED EXTENSIONS
        /minimus.c$
        /ext/console.c$
        /ext/mw/

        # Game must bring its own header
        /rom_header.c$
    )
    foreach(_excl ${SGDK_PAL_REPLACED})
        list(FILTER SGDK_TRANSLATED_SOURCES EXCLUDE REGEX "${_excl}")
    endforeach()


    # Translated SGDK sources get their own object library so the translated
    # inc/ path and NEWBASE_SGDK are strictly private to them and do not
    # leak into the PAL implementation files (sgdk.cpp, api.cpp, e_vdp.cpp).
    add_library(newbase_sys_sgdk_libs OBJECT ${SGDK_TRANSLATED_SOURCES} "${SGDK_TRANSLATED_DIR}/res/libres.s")
    set(_sgdk_lib_inc "${SGDK_TRANSLATED_DIR}/inc" "${NEWBASE_ROOT}/include/newbase/sgdk/api" "${SGDK_TRANSLATED_DIR}")  # translated dir root is for res/libres.h
    if(DEFINED SGDK_BUILTIN_RES_INCLUDE_DIR)
        list(APPEND _sgdk_lib_inc "${SGDK_BUILTIN_RES_INCLUDE_DIR}")
        add_dependencies(newbase_sys_sgdk_libs newbase_sgdk_builtin_res)
    endif()
    target_include_directories(newbase_sys_sgdk_libs PRIVATE ${_sgdk_lib_inc})
    target_compile_definitions(newbase_sys_sgdk_libs PRIVATE NEWBASE_SGDK)
    target_compile_options(newbase_sys_sgdk_libs PRIVATE -Wno-builtin-declaration-mismatch)
    export(TARGETS newbase_sys_sgdk_libs NAMESPACE nb:: FILE newbase-sgdk-lib-export.cmake)





    ## NEWBASE SYSTEM
    # Includes translated objects but keeps its own clean include scope
    newbase_add_system(NAME sgdk SOURCES
            ./sgdk.cpp
            ./e_vdp.cpp
            ./api/api.cpp
            ./api/string.c
            $<TARGET_OBJECTS:newbase_sys_sgdk_libs>
        )
    target_link_libraries(newbase_sys_sgdk PUBLIC ymfm)
    # PAL sources include <newbase/sgdk/api/*.h> which in turn include "*.patched.h"
    # — those files live in the translated inc dir.
    target_include_directories(newbase_sys_sgdk PRIVATE "${SGDK_TRANSLATED_DIR}/inc" ../include/newbase/sgdk/api)
    newbase_commit_systems()

endif(NEWBASE_SGDK)
