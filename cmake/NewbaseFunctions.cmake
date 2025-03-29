# cmake functions used by newbase
set(NEWBASE_ALL_SYSTEMS
    script_lua
    render_simple
    audio
    )

function(newbase_add_executable)
    set(options "")
    set(oneValueArgs TARGET)
    set(multiValueArgs SOURCES)
    cmake_parse_arguments(PARSE_ARGV 0 arg 
        "${options}" "${oneValueArgs}" "${multiValueArgs}")

    if(NOT DEFINED arg_TARGET)
        message(FATAL_ERROR "[newbase_prepare_executable] no target was given!")
    endif()
    if(NOT DEFINED arg_SOURCES)
        message(FATAL_ERROR "[newbase_prepare_executable] no sources were given!")
    endif()

    # TODO check if this target is the android main executable
    if(NEWBASE_ANDROID_MAIN STREQUAL "${arg_TARGET}")
        add_library(main SHARED ${arg_SOURCES})
        add_library(${arg_TARGET} ALIAS main)
        set(arg_TARGET main)
    else()
        add_executable(${arg_TARGET} WIN32 ${arg_SOURCES})
    endif()

    target_link_libraries(${arg_TARGET} PRIVATE newbase)
    
endfunction()

# TODO fold into newbase_add_executable
function(newbase_prepare_executable)
    set(options BUILD_SYMLINKS NO_INSTALL)
    set(oneValueArgs TARGET)
    set(multiValueArgs SYSTEMS)
    cmake_parse_arguments(PARSE_ARGV 0 arg 
        "${options}" "${oneValueArgs}" "${multiValueArgs}")

    if(NOT DEFINED arg_TARGET)
        message(FATAL_ERROR "[newbase_prepare_executable] no target was given!")
    endif()
    if((NOT DEFINED arg_SYSTEMS))
        message(FATAL_ERROR "[newbase_prepare_executable] no systems are listed for target '${arg_TARGET}'")
    endif()

    if(NEWBASE_ANDROID_MAIN STREQUAL "${arg_TARGET}")
        # do stuff to "main" target instead
        set(arg_TARGET main)
    endif()

    if(DEFINED EMSCRIPTEN)
        set(target_opts "-sALLOW_MEMORY_GROWTH")
        message("[newbase_prepare_executable] setting emscripten options for '${arg_TARGET}': ${target_opts}")
        target_link_options(${arg_TARGET} PRIVATE ${target_opts})
    endif()

    # implement dynamic system lists
    list(LENGTH arg_SYSTEMS arg_SYSTEMS_LEN)
    message(${arg_SYSTEMS_LEN})
    if(arg_SYSTEMS_LEN EQUAL 1)
        if(arg_SYSTEMS STREQUAL "ALL")
            set(arg_SYSTEMS ${NEWBASE_ALL_SYSTEMS})
        elseif(arg_SYSTEMS STREQUAL "AUTO")
            execute_process(
                COMMAND "${CMAKE_SOURCE_DIR}/scripts/config_get_systems.py" 
                        "${CMAKE_CURRENT_SOURCE_DIR}/config.yaml"
                TIMEOUT 5
                OUTPUT_VARIABLE arg_SYSTEMS
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            message("[newbase_prepare_executable] got systems from 'config.yaml' for '${arg_TARGET}': ${arg_SYSTEMS}")
            set(rtti_extra_depends "${CMAKE_CURRENT_SOURCE_DIR}/config.yaml")
        endif()
    endif()

    set(rtti_entry_target ${arg_TARGET}_rtti_entry_gen)
    set(rtti_entry_file_template ${CMAKE_SOURCE_DIR}/include/newbase/reflection/initialization_template.h.in)
    set(rtti_entry_file_output ${CMAKE_CURRENT_BINARY_DIR}/include/newbase/reflection/init.h)
    message("[newbase_prepare_executable] RTTI entry points: from '${rtti_entry_file_template}'")
    message("[newbase_prepare_executable] RTTI entry points: to '${rtti_entry_file_output}'")
    add_custom_target( ${rtti_entry_target} ALL
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/codegen_rtti_entry_points.py
            "${rtti_entry_file_template}" 
            "${rtti_entry_file_output}" 
            "${arg_SYSTEMS}"
        BYPRODUCTS "${rtti_entry_file_output}"
        DEPENDS "${rtti_entry_file_template}" "${rtti_extra_depends}"
        VERBATIM
    )
    add_dependencies(${arg_TARGET} ${rtti_entry_target})
    target_include_directories(${arg_TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)

endfunction()

function(newbase_declare_resources)
    set(options BUILD_SYMLINKS NO_INSTALL INDEX)
    set(oneValueArgs TARGET)
    set(multiValueArgs FILES)
    cmake_parse_arguments(PARSE_ARGV 0 arg 
        "${options}" "${oneValueArgs}" "${multiValueArgs}")

    message("[newbase_declare_resources] from: " ${CMAKE_CURRENT_SOURCE_DIR})
    message("[newbase_declare_resources] destination prefix is: " ${NEWBASE_DEFAULT_RES_PREFIX})

    if(NEWBASE_ANDROID_MAIN STREQUAL "${arg_TARGET}")
        # do stuff to "main" target instead
        set(arg_TARGET main)
    endif()

    if(arg_BUILD_SYMLINKS)
        set(links_dest_dir "${CMAKE_CURRENT_BINARY_DIR}/${NEWBASE_DEFAULT_RES_PREFIX}")
        message("[newbase_declare_resources] ensuring symlink dest dir: ${links_dest_dir}")
        file(MAKE_DIRECTORY "${links_dest_dir}")
    endif()

    foreach(file ${arg_FILES})
        message("[newbase_declare_resources] file/folder: " ${file})
        file(REAL_PATH "${file}" file_real BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        if(DEFINED EMSCRIPTEN)
            if(DEFINED arg_TARGET)
            message("[newbase_declare_resources] \tembedding to emscripten executable target")
                target_link_options(${arg_TARGET} PRIVATE "--embed-file=${file_real}@${NEWBASE_DEFAULT_RES_PREFIX}/${file}")
            endif()
        endif()
        if(arg_BUILD_SYMLINKS)
            message("[newbase_declare_resources] \tadding symlink to binary dir")
            file(RELATIVE_PATH file_build_rel "${links_dest_dir}" "${file_real}")
            message("[newbase_declare_resources] \treal path: " ${file_real})
            message("[newbase_declare_resources] \tbuild-relative path: " ${file_build_rel})
            file(CREATE_LINK "${file_build_rel}" "${links_dest_dir}/${file}" SYMBOLIC)
        endif()
    endforeach()

    if(arg_INDEX)
    message("[newbase_declare_resources] creating target index")
        set(res_index_target ${arg_TARGET}_res_index)
        add_custom_target( ${res_index_target} ALL
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/res_indexer.py
                "${links_dest_dir}"
            # BYPRODUCTS index file, if needed (?)
            # DEPENDS if another target is processing or generating resources
            VERBATIM
        )
        add_dependencies(${arg_TARGET} ${res_index_target})
    endif()

endfunction()