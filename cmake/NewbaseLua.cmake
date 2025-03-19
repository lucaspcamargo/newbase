include( ExternalProject )
# TODO remove submodule and just fetch the tarball: lua-5.4.7.tar.gz 	2024-06-13 	374097 	9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30
# URL is https://www.lua.org/ftp/lua-5.4.7.tar.gz
ExternalProject_Add( lua_ext 
    URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
    URL_HASH SHA256=9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30
    DOWNLOAD_EXTRACT_TIMESTAMP true
    CONFIGURE_COMMAND ""
    BUILD_COMMAND make
    BUILD_IN_SOURCE true
    BUILD_JOB_SERVER_AWARE true
    BUILD_BYPRODUCTS ${CMAKE_SOURCE_DIR}/vendored/lua/liblua.a
    INSTALL_COMMAND "")

ExternalProject_Get_Property( lua_ext SOURCE_DIR )
set( LUA_SRC "${SOURCE_DIR}")
unset( SOURCE_DIR )
message( "[newbase_lua] Lua is at ${LUA_SRC}" )

add_library( lua STATIC IMPORTED )
set_target_properties( lua PROPERTIES IMPORTED_LOCATION "${LUA_SRC}/src/liblua.a" )
set( LUA_INCLUDES "${LUA_SRC}/src/" )

# makes no sense to check for luac existence before build, since package is not yet obtained
set( LUAC_EXPECTED_PATH "${LUA_SRC}/src/luac" )
#if(EXISTS "${LUAC_EXPECTED_PATH}.c")
message("[newbase_lua] luac will be at: '${LUAC_EXPECTED_PATH}'")
cmake_path( ABSOLUTE_PATH LUAC_EXPECTED_PATH OUTPUT_VARIABLE LUAC_BIN )
#else()
#    message("[newbase_lua] Could not find luac.c at: '${LUAC_EXPECTED_PATH}.c', LUAC_BIN will be undefined")
#endif()