# cmake functions used by newbase

function(newbase_declare_resources)
    set(options BUILD_SYMLINKS NO_INSTALL)
    set(oneValueArgs "")
    set(multiValueArgs FILES)
    cmake_parse_arguments(PARSE_ARGV 0 arg 
        "${options}" "${oneValueArgs}" "${multiValueArgs}")

    message("[newbase_declare_resources] from: " ${CMAKE_CURRENT_SOURCE_DIR})

    foreach(file ${arg_FILES})
        message("[newbase_declare_resources] file/folder: " ${file})
        file(REAL_PATH "${file}" file_real BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        if(arg_BUILD_SYMLINKS)
            message("[newbase_declare_resources] \tadding symlink to binary dir")
            file(RELATIVE_PATH file_build_rel "${CMAKE_CURRENT_BINARY_DIR}" "${file_real}")
            message("[newbase_declare_resources] \treal path: " ${file_real})
            message("[newbase_declare_resources] \tbuild-relative path: " ${file_build_rel})
            file(CREATE_LINK "${file_build_rel}" "${CMAKE_CURRENT_BINARY_DIR}/${file}" SYMBOLIC)
        endif()
    endforeach()

endfunction()