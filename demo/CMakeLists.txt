add_executable(newbase_demo WIN32 
    src/main.c)

target_link_libraries(newbase_demo PRIVATE newbase)

newbase_declare_resources(FILES res config.yaml BUILD_SYMLINKS)