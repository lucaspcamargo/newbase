# Add render_simple system

newbase_add_system(NAME render_simple SOURCES 
    ./render_simple.cpp
    ./lua_bindings.cpp
)
newbase_commit_systems()