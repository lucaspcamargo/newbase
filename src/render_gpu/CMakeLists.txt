newbase_add_system(NAME render_gpu SOURCES
    ./render_gpu.cpp
)

target_link_libraries(newbase_sys_render_gpu PRIVATE
    SDL3_shadercross::SDL3_shadercross-static
)

newbase_commit_systems()
