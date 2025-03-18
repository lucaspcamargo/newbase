include(ExternalProject)

ExternalProject_Add(lua 
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/vendored/lua
    CONFIGURE_COMMAND ""
    BUILD_COMMAND make
    BUILD_IN_SOURCE true
    BUILD_JOB_SERVER_AWARE true
    INSTALL_COMMAND "")