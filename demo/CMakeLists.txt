add_executable(newbase_demo WIN32 
    src/main.c)

target_link_libraries(newbase_demo PRIVATE newbase)