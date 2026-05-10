if(NOT NEWBASE_TALKIE_PCM)
    message(STATUS "[lpcvocab_gen] skipping (NEWBASE_TALKIE_PCM is OFF)")
    return()
endif()

add_executable(lpcvocab_gen main.cpp)

target_compile_definitions(lpcvocab_gen PRIVATE NEWBASE_TALKIE_PCM)

target_include_directories(lpcvocab_gen PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/vendored/TalkiePCM/src"
    # Generated talkie_pcm_vocab.hpp lives in the binary dir's include subtree
    "${CMAKE_BINARY_DIR}/include"
)

set_target_properties(lpcvocab_gen PROPERTIES
    EXCLUDE_FROM_ALL FALSE
)
