# Add clock system

newbase_add_system(NAME clock SOURCES 
    ./clock.cpp
    ./lua_bindings.cpp
)
newbase_commit_systems()