add_executable(newbase_demo WIN32 
    src/main.c)

target_link_libraries(newbase_demo PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,newbase>") # workaround for rtti static init
newbase_prepare_executable(TARGET newbase_demo SYSTEMS render_simple audio script_lua)
newbase_declare_resources(TARGET newbase_demo BUILD_SYMLINKS FILES res config.yaml)