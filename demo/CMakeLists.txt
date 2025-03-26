newbase_add_executable(
    TARGET 
        newbase_demo 
    SOURCES 
        src/main.c
)
newbase_prepare_executable(TARGET newbase_demo SYSTEMS render_simple audio script_lua)
newbase_declare_resources(TARGET newbase_demo BUILD_SYMLINKS FILES res config.yaml)