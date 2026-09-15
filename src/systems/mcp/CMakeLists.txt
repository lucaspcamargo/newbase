newbase_add_system(NAME mcp SOURCES
    ./mcp.cpp
)

target_link_libraries(newbase_sys_mcp PRIVATE httplib::httplib)

# newbase_add_system compiles every system with -fno-exceptions by default.
# This system's own error handling (catching ryml parse errors on malformed
# network input, rather than letting them abort() the engine — see the
# comment on parse_json_safe() in mcp.cpp) genuinely needs real try/catch.
if(NOT MSVC)
    target_compile_options(newbase_sys_mcp PRIVATE -fexceptions)
endif()

newbase_commit_systems()
