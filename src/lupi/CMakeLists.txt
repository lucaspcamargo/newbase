# Add Lupi compatibility system

newbase_add_system(NAME lupi SOURCES
    ./lupi.cpp
    ./api_ui.cpp
    ./api_input.cpp
    ./api_stubs.cpp
    ./api_require.cpp
    ./draw.cpp
    ./loaders.cpp
    ./tiled_maps.cpp
)
newbase_commit_systems()
