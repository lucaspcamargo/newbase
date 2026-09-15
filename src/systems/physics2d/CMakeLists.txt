# Add physics2d system

newbase_add_system(NAME physics2d SOURCES 
    ./physics2d.cpp
    ./debug_draw.cpp
)
newbase_commit_systems()