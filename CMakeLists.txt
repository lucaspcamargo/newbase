cmake_minimum_required(VERSION 3.5)
project(newbase)

option(NB_VENDORED "Use vendored libraries" ON)

if(NB_VENDORED)
    add_subdirectory(vendored/SDL EXCLUDE_FROM_ALL)
else()
    find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3-shared)
endif()

add_executable(newbase WIN32 src/main.c
    src/stb_impl.c)

target_include_directories(newbase PUBLIC vendored/stb)

target_link_libraries(newbase PRIVATE SDL3::SDL3 -lm)
