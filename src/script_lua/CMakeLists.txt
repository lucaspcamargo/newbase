# Add script_lua system

newbase_add_system(NAME script_lua SOURCES
    ./script_lua.cpp
    ./bindings.cpp
    ./bindings_glm.cpp
)
newbase_commit_systems()