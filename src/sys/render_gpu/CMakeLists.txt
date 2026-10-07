newbase_add_system(NAME render_gpu SOURCES
    ./render_gpu.cpp
    ./pipelines.cpp
    ./shaders.cpp
    ./targets.cpp
    ./textures.cpp
    ./util2d.cpp
    ./rtti.cpp
)


if(NEWBASE_SLANG_RUNTIME)
target_link_libraries(newbase_sys_render_gpu PUBLIC slang)
endif(NEWBASE_SLANG_RUNTIME)

newbase_commit_systems()
