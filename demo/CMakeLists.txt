add_executable(newbase_demo WIN32 
    src/main.c)

target_link_libraries(newbase_demo PRIVATE newbase)
newbase_prepare_executable(newbase_demo)
newbase_declare_resources(TARGET newbase_demo BUILD_SYMLINKS FILES res config.yaml)