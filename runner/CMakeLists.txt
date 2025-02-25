add_executable(newbase_runner WIN32 
    src/main.c)

target_link_libraries(newbase_runner PRIVATE newbase)