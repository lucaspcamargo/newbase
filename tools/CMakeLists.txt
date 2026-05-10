if(CMAKE_CROSSCOMPILING)
    message(STATUS "[tools] skipping host tools (cross-compiling)")
    return()
endif()

add_subdirectory(lpcvocab_gen)
