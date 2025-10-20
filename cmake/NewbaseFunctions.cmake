# cmake functions used by newbase

# this variable should be set to a list of all systems as they are regsitered and enabled 
set(NEWBASE_ALL_SYSTEMS "")

# check whether newbase is being imported
# set root dir accordingly
set(NEWBASE_IMPORTED FALSE) # not considered imported by default
set(NEWBASE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(NEWBASE_TARGET "newbase")
if(TARGET nb::newbase)
    set(NEWBASE_TARGET "nb::newbase")
    get_target_property(NEWBASE_IMPORTED nb::newbase IMPORTED)
    message("[newbase_functions] nb::newbase IMPORTED is ${NEWBASE_IMPORTED}")
    if(NEWBASE_IMPORTED)
        set(NEWBASE_ROOT "../newbase") # HACK: there must be a better way to get this path
        message("[newbase_functions] newbase imported from: ${NEWBASE_IMPORT_ROOT}")
    else()
        message("[newbase_functions] newbase already exists, and not imported...")
    endif()
else()
    message("[newbase_functions] nb::newbase target not found, assuming not imported...")
endif()
message("[newbase_functions] newbase root dir is: ${NEWBASE_ROOT}")



if(WIN32 AND NOT CMAKE_CROSSCOMPILING)
    set(PYTHON_INTERPRETER python.exe)
endif()

function(newbase_add_executable)
    set(options "")
    set(oneValueArgs TARGET)
    set(multiValueArgs SOURCES)
    cmake_parse_arguments(PARSE_ARGV 0 arg 
        "${options}" "${oneValueArgs}" "${multiValueArgs}")

    if(NOT DEFINED arg_TARGET)
        message(FATAL_ERROR "[newbase_add_executable] no target was given!")
    endif()
    if(NOT DEFINED arg_SOURCES)
        message(FATAL_ERROR "[newbase_add_executable] no sources were given!")
    endif()

    # TODO check if this target is the android main executable
    if(NEWBASE_ANDROID_MAIN STREQUAL "${arg_TARGET}")
        add_library(main SHARED ${arg_SOURCES})
        add_library(${arg_TARGET} ALIAS main)
        set(arg_TARGET main)
    else()
        add_executable(${arg_TARGET} WIN32 ${arg_SOURCES})
    endif()

    target_link_libraries(${arg_TARGET} PRIVATE ${NEWBASE_TARGET})
    
endfunction()

# TODO fold into newbase_add_executable, maybe?
function(newbase_prepare_executable)
    set(options BUILD_SYMLINKS NO_INSTALL NO_CORE_RESOURCES)
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
        set(target_opts -sALLOW_MEMORY_GROWTH -sEXPORTED_FUNCTIONS=_main,_free -sENVIRONMENT=web)
        message("[newbase_prepare_executable] setting emscripten options for '${arg_TARGET}': ${target_opts}")
        target_link_options(${arg_TARGET} PRIVATE ${target_opts})
    endif()

    # implement dynamic system lists
    list(LENGTH arg_SYSTEMS arg_SYSTEMS_LEN)
    if(arg_SYSTEMS_LEN EQUAL 1)
        if(arg_SYSTEMS STREQUAL "ALL")
            set(arg_SYSTEMS ${NEWBASE_ALL_SYSTEMS})
        elseif(arg_SYSTEMS STREQUAL "AUTO")
            execute_process(
                COMMAND ${PYTHON_INTERPRETER}
                        "${CMAKE_SOURCE_DIR}/scripts/config_get_systems.py" 
                        "${CMAKE_CURRENT_SOURCE_DIR}/config.yaml"
                TIMEOUT 5
                OUTPUT_VARIABLE arg_SYSTEMS
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            message("[newbase_prepare_executable] got systems from 'config.yaml' for '${arg_TARGET}': ${arg_SYSTEMS}")
            set(rtti_extra_depends "${CMAKE_CURRENT_SOURCE_DIR}/config.yaml")
        endif()
    endif()

    # filter list of systems according to what is available
    set(filtered_systems "")
    foreach(system ${arg_SYSTEMS})
        list(FIND NEWBASE_ALL_SYSTEMS ${system} system_index)
        if(NOT system_index EQUAL -1)
            list(APPEND filtered_systems ${system})
            message("[newbase_prepare_executable] will generate rtti info for system ${system}...")
            message("${filtered_systems}")
        else()
            message(WARNING "[newbase_prepare_executable] system '${system}' is not available for target '${arg_TARGET}'. Has the system been registered? Won't be linked, RTTI hooks skipped")
            message("ALL SYSTEMS: ${NEWBASE_ALL_SYSTEMS}")
        endif()
    endforeach()

    set(rtti_entry_target ${arg_TARGET}_rtti_entry_gen)
    set(rtti_entry_file_template ${CMAKE_SOURCE_DIR}/include/newbase/reflection/initialization_template.h.in)
    set(rtti_entry_file_output ${CMAKE_CURRENT_BINARY_DIR}/include/newbase/reflection/init.h)
    message("[newbase_prepare_executable] RTTI entry points: from '${rtti_entry_file_template}'")
    message("[newbase_prepare_executable] RTTI entry points: to '${rtti_entry_file_output}'")
    add_custom_target( ${rtti_entry_target}
        COMMAND ${PYTHON_INTERPRETER}
            ${CMAKE_SOURCE_DIR}/scripts/codegen_rtti_entry_points.py
            "${rtti_entry_file_template}" 
            "${rtti_entry_file_output}" 
            "${filtered_systems}"
        BYPRODUCTS "${rtti_entry_file_output}"
        DEPENDS "${rtti_entry_file_template}" ${rtti_extra_depends}
        VERBATIM
    )
    add_dependencies(${arg_TARGET} ${rtti_entry_target})
    target_include_directories(${arg_TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)

    # now, link the systems to the executable
    foreach(system ${filtered_systems})
        set(system_target newbase_sys_${system})
        if(TARGET ${system_target})
            message("[newbase_prepare_executable] linking system '${system}' to target '${arg_TARGET}'")
            target_link_libraries(${arg_TARGET} PUBLIC ${system_target})
        else()
            message(FATAL_ERROR "[newbase_prepare_executable] system '${system}' (target '${system_target}') is not available to link to executable target '${arg_TARGET}'. System is registered but its target was not found.")
        endif()
    endforeach()
endfunction()

function(newbase_add_system)
    set(options)
    set(oneValueArgs NAME)
    set(multiValueArgs SOURCES)
    cmake_parse_arguments(PARSE_ARGV 0 arg 
        "${options}" "${oneValueArgs}" "${multiValueArgs}")

    if(NOT DEFINED arg_NAME)
        message(FATAL_ERROR "[newbase_add_system] no system name was given!")
    endif()
    if(NOT DEFINED arg_SOURCES)
        message(FATAL_ERROR "[newbase_add_system] no sources were given for system '${arg_NAME}'!")
    endif()

    set(system_target newbase_sys_${arg_NAME})
    message("[newbase_add_system] adding system target '${system_target}' with sources: ${arg_SOURCES}")
    add_library(${system_target} OBJECT ${arg_SOURCES})
    target_compile_options(${system_target} PUBLIC -fno-exceptions)
    target_link_libraries(${system_target} PRIVATE ${NEWBASE_TARGET})

    list(FIND NEWBASE_ALL_SYSTEMS ${arg_NAME} system_index)
    if(system_index EQUAL -1)
        list(APPEND NEWBASE_ALL_SYSTEMS ${arg_NAME})
        message("[newbase_add_system] registered system '${arg_NAME}' (all systems: ${NEWBASE_ALL_SYSTEMS})")
    else()
        message(FATAL_ERROR "[newbase_add_system] system '${arg_NAME}' was already registered!")
    endif()
    set(NEWBASE_ALL_SYSTEMS ${NEWBASE_ALL_SYSTEMS} PARENT_SCOPE) # update system list in calling scope 
endfunction()

# TODO: make the systems list a property of the "newbase" target, so we can eliminate this macro
macro(newbase_commit_systems)
    set(NEWBASE_ALL_SYSTEMS ${NEWBASE_ALL_SYSTEMS} PARENT_SCOPE) # update system list in calling scope 
endmacro()

function(newbase_declare_resources)
    set(options BUILD_SYMLINKS NO_INSTALL INDEX NO_CORE_RESOURCES)
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

    # add core files to file list, unless NO_CORE_RESOURCES is set
    if(NOT arg_NO_CORE_RESOURCES)
        list(APPEND arg_FILES "${CMAKE_CURRENT_LIST_DIR}/../res/_nb_core")
    endif()

    foreach(file ${arg_FILES})
        message("[newbase_declare_resources] file/folder: " ${file})
        file(REAL_PATH "${file}" file_real BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        get_filename_component(file_basename "${file}" NAME)
        if(DEFINED EMSCRIPTEN)
            if(DEFINED arg_TARGET)
            message("[newbase_declare_resources] \tembedding to emscripten executable target")
                target_link_options(${arg_TARGET} PRIVATE "--embed-file=${file_real}@${NEWBASE_DEFAULT_RES_PREFIX}/${file_basename}")
            endif()
        endif()
        if(arg_BUILD_SYMLINKS)
            message("[newbase_declare_resources] \tadding symlink to binary dir")
            file(RELATIVE_PATH file_build_rel "${links_dest_dir}" "${file_real}")
            message("[newbase_declare_resources] \treal path: " ${file_real})
            message("[newbase_declare_resources] \tbuild-relative path: " ${file_build_rel})
            if(WIN32)
                # hack: copy instead
                message("[newbase_declare_resources] \tWIN32: COPY" ${file_real} DESTINATION ${links_dest_dir})
                file(COPY ${file_real} DESTINATION ${links_dest_dir})
            else()
                file(CREATE_LINK "${file_build_rel}" "${links_dest_dir}/${file_basename}" SYMBOLIC)
            endif()
        endif()
    endforeach()

    if(arg_INDEX)
    message("[newbase_declare_resources] creating target index. Resources are at: ${links_dest_dir}")
        set(res_index_target ${arg_TARGET}_res_index)
        add_custom_target( ${res_index_target} ALL
            COMMAND ${PYTHON_INTERPRETER}
                ${CMAKE_SOURCE_DIR}/scripts/res_indexer.py
                "${links_dest_dir}"
            # BYPRODUCTS index file, if needed (?)
            # DEPENDS if another target is processing or generating resources
            VERBATIM
        )
        add_dependencies(${arg_TARGET} ${res_index_target})
    endif()
endfunction()