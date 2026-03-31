# Functions and variables for SGDK API support

# ---------------------------------------------------------------------------
# _newbase_sgdk_rescomp(RES_FILE <path> OUT_DIR <dir> TARGET <name>)
#
# Runs rescomp on a single .res file as a build-time custom command and
# creates a custom target <name> that other targets can depend on.
# Requires Java; aborts if rescomp.jar is missing.
# ---------------------------------------------------------------------------
function(_newbase_sgdk_rescomp)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "RES_FILE;OUT_DIR;TARGET" "")

    if(NOT DEFINED arg_RES_FILE OR NOT DEFINED arg_OUT_DIR OR NOT DEFINED arg_TARGET)
        message(FATAL_ERROR "[_newbase_sgdk_rescomp] RES_FILE, OUT_DIR and TARGET are all required")
    endif()

    find_program(Java_JAVA_EXECUTABLE java REQUIRED)

    set(_rescomp_jar "${NEWBASE_SGDK_PATH}/bin/rescomp.jar")
    if(NOT EXISTS "${_rescomp_jar}")
        message(FATAL_ERROR "[newbase] rescomp.jar not found at ${_rescomp_jar}")
    endif()

    get_filename_component(_stem "${arg_RES_FILE}" NAME_WE)
    set(_out_s "${arg_OUT_DIR}/${_stem}.s")
    set(_out_h "${arg_OUT_DIR}/${_stem}.h")

    add_custom_command(
        OUTPUT  "${_out_s}" "${_out_h}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${arg_OUT_DIR}"
        COMMAND "${Java_JAVA_EXECUTABLE}" -jar "${_rescomp_jar}"
                "${arg_RES_FILE}" "${_out_s}"
        DEPENDS "${arg_RES_FILE}"
        COMMENT "[sgdk] rescomp ${arg_RES_FILE}"
        VERBATIM
    )

    add_custom_target(${arg_TARGET}
        DEPENDS "${_out_s}" "${_out_h}"
    )
endfunction()


if(NEWBASE_SGDK)

    if(NOT EXISTS "${NEWBASE_SGDK_PATH}")
        message(FATAL_ERROR "[newbase] NEWBASE_SGDK is ON but NEWBASE_SGDK_PATH does not exist: ${NEWBASE_SGDK_PATH}")
    endif()
    message("[newbase] SGDK path: ${NEWBASE_SGDK_PATH}")

    # ymfm library
    # we build only what we need for SGDK support (core and OPN)
    add_library(ymfm STATIC EXCLUDE_FROM_ALL
        vendored/ymfm/src/ymfm_adpcm.cpp
        vendored/ymfm/src/ymfm_fm.ipp
        vendored/ymfm/src/ymfm_opn.cpp
        vendored/ymfm/src/ymfm_pcm.cpp
        vendored/ymfm/src/ymfm_ssg.cpp
    )
    target_link_directories(ymfm PUBLIC vendored/ymfm/src )
    export(TARGETS ymfm NAMESPACE nb:: FILE newbase-ymfm-export.cmake)

    # Run sgdk_translator.py at configure time to populate build/sgdk_translated/
    # with guarded headers and rewritten sources.
    set(SGDK_TRANSLATED_DIR "${CMAKE_BINARY_DIR}/sgdk_translated")

    message("[newbase] translating SGDK sources into ${SGDK_TRANSLATED_DIR}")
    execute_process(
        COMMAND "${NEWBASE_PYTHON_INTERPRETER}"
                "${CMAKE_SOURCE_DIR}/scripts/sgdk_translator.py"
                --sgdk-root  "${NEWBASE_SGDK_PATH}"
                --out-root   "${SGDK_TRANSLATED_DIR}"
        RESULT_VARIABLE _sgdk_translator_result
        OUTPUT_VARIABLE _sgdk_translator_output
        ERROR_VARIABLE  _sgdk_translator_error
    )
    if(NOT _sgdk_translator_result EQUAL 0)
        message(FATAL_ERROR "[newbase] sgdk_translator.py failed:\n${_sgdk_translator_error}")
    endif()
    if(_sgdk_translator_output)
        message(STATUS "${_sgdk_translator_output}")
    endif()


    # SGDK built-in resources (default font, logos, etc.)
    # rescomp generates a .h (extern declarations) + .s (m68k asm, not compiled on PC).
    # The header is exposed to newbase_sys_sgdk_libs via SGDK_BUILTIN_RES_INCLUDE_DIR.
    set(_sgdk_builtin_res "${NEWBASE_SGDK_PATH}/res/libres.res")
    if(EXISTS "${_sgdk_builtin_res}")
        set(SGDK_BUILTIN_RES_INCLUDE_DIR "${SGDK_TRANSLATED_DIR}/res")
        _newbase_sgdk_rescomp(
            RES_FILE "${_sgdk_builtin_res}"
            OUT_DIR  "${SGDK_BUILTIN_RES_INCLUDE_DIR}"
            TARGET   newbase_sgdk_builtin_res
        )
        message("[newbase] SGDK built-in resources -> ${SGDK_BUILTIN_RES_INCLUDE_DIR}")
    else()
        message(WARNING "[newbase] SGDK built-in res not found at ${_sgdk_builtin_res} — skipping")
    endif()


endif()


# newbase_sgdk_add_game(
#   TARGET    <name>
#   SOURCES   <file> [<file>...]
#   MAIN      <file>        # the source file containing main(); its main symbol
#                           # is renamed to nb_sgdk_main so the compatibility
#                           # layer's own main() can call it after setup.
#   RESOURCES <file.res>    # optional
# )
#
# Creates an executable target for a SGDK game running on the PC compatibility
# layer.  rescomp is invoked at build time to process the .res file; the
# generated header is made available to the target and the generated assembly
# is noted but not compiled (it is m68k GAS and not usable on the host).
function(newbase_sgdk_add_game)
    if(NOT NEWBASE_SGDK)
        message(FATAL_ERROR "[newbase_sgdk_add_game] NEWBASE_SGDK is OFF")
    endif()

    set(options "")
    set(oneValueArgs TARGET RESOURCES MAIN)
    set(multiValueArgs SOURCES)
    cmake_parse_arguments(PARSE_ARGV 0 arg
        "${options}" "${oneValueArgs}" "${multiValueArgs}")

    if(NOT DEFINED arg_TARGET)
        message(FATAL_ERROR "[newbase_sgdk_add_game] TARGET is required")
    endif()
    if(NOT DEFINED arg_SOURCES)
        message(FATAL_ERROR "[newbase_sgdk_add_game] SOURCES is required")
    endif()
    if(NOT DEFINED arg_MAIN)
        message(FATAL_ERROR "[newbase_sgdk_add_game] MAIN is required")
    endif()
    if(NOT DEFINED arg_RESOURCES)
        message(WARNING "[newbase_sgdk_add_game] RESOURCES is not set, not adding resources to game target")
    endif()


    # ----------------------------------------------------
    # rescomp step
    # ----------------------------------------------------
    set(_rescomp_out_dir "")
    set(_rescomp_target  "")
    if(DEFINED arg_RESOURCES)
        # resolve the .res path relative to the caller's source dir
        get_filename_component(_res_abs "${arg_RESOURCES}" ABSOLUTE
                               BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        set(_rescomp_out_dir "${CMAKE_CURRENT_BINARY_DIR}/res_generated")
        set(_rescomp_target  "${arg_TARGET}_rescomp")
        _newbase_sgdk_rescomp(
            RES_FILE "${_res_abs}"
            OUT_DIR  "${_rescomp_out_dir}"
            TARGET   "${_rescomp_target}"
        )
    endif()

    # ----------------------------------------------------
    # Game Target
    # ----------------------------------------------------
    add_library(${arg_TARGET} STATIC ${arg_SOURCES} ${arg_MAIN})

    if(DEFINED arg_RESOURCES)
        add_dependencies(${arg_TARGET} ${_rescomp_target})
    endif()

    # Expose the generated header to the game's sources
    set(_inc_dirs "${SGDK_TRANSLATED_DIR}/inc" "${NEWBASE_ROOT}/include/newbase/sgdk/api")
    if(_rescomp_out_dir)
        list(APPEND _inc_dirs "${_rescomp_out_dir}")
    endif()
    if(DEFINED SGDK_BUILTIN_RES_INCLUDE_DIR)
        list(APPEND _inc_dirs "${SGDK_BUILTIN_RES_INCLUDE_DIR}")
        add_dependencies(${arg_TARGET} newbase_sgdk_builtin_res)
    endif()
    target_include_directories(${arg_TARGET} PRIVATE ${_inc_dirs})

    # Link against newbase and the SGDK support layer
    target_link_libraries(${arg_TARGET} PRIVATE ${NEWBASE_TARGET} ${SGDK_SUPPORT_LIBS})

    # Compile with NEWBASE_SGDK so SGDK hardware macros are overridden
    target_compile_definitions(${arg_TARGET} PRIVATE NEWBASE_SGDK "static=static thread_local")

    # Rename main() -> nb_sgdk_main() in the game's entry-point file so the
    # compatibility layer's own main() can call it after platform setup.
    get_filename_component(_main_abs "${arg_MAIN}" ABSOLUTE
                           BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    set_source_files_properties("${_main_abs}" PROPERTIES
        COMPILE_DEFINITIONS "main=nb_sgdk_main"
    )

    message("[newbase_sgdk_add_game] target '${arg_TARGET}' | res -> ${_rescomp_out_dir}")
endfunction(newbase_sgdk_add_game)

