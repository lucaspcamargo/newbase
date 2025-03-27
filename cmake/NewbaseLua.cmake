function( newbase_lua_setup_ext )
    include( ExternalProject )
    # TODO remove submodule and just fetch the tarball: lua-5.4.7.tar.gz 	2024-06-13 	374097 	9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30
    # URL is https://www.lua.org/ftp/lua-5.4.7.tar.gz
    ExternalProject_Add( lua_ext
        URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
        URL_HASH SHA256=9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30
    #    DOWNLOAD_EXTRACT_TIMESTAMP true <-- too new for android cmake
        CONFIGURE_COMMAND ""
        BUILD_COMMAND make
        BUILD_IN_SOURCE true
        BUILD_JOB_SERVER_AWARE true
        BUILD_BYPRODUCTS "${CMAKE_BINARY_DIR}/lua_ext-prefix/src/lua_ext/src/liblua.a"
        INSTALL_COMMAND "")

    ExternalProject_Get_Property( lua_ext SOURCE_DIR )
    set( LUA_SRC "${SOURCE_DIR}")
    unset( SOURCE_DIR )
    message( "[newbase_lua_setup_ext] Lua is at ${LUA_SRC}" )

    add_library( lua STATIC IMPORTED )
    add_dependencies(lua lua_ext)
    set_target_properties( lua PROPERTIES IMPORTED_LOCATION "${LUA_SRC}/src/liblua.a" )
    set( LUA_INCLUDES "${LUA_SRC}/src/" PARENT_SCOPE )

    # makes no sense to check for luac existence before build, since package is not yet obtained
    set( LUAC_EXPECTED_PATH "${LUA_SRC}/src/luac" )
    #if(EXISTS "${LUAC_EXPECTED_PATH}.c")
    message("[newbase_lua_setup_ext] luac will be at: '${LUAC_EXPECTED_PATH}'")
    cmake_path( ABSOLUTE_PATH LUAC_EXPECTED_PATH OUTPUT_VARIABLE LUAC_BIN )
    #else()
    #    message("[newbase_lua] Could not find luac.c at: '${LUAC_EXPECTED_PATH}.c', LUAC_BIN will be undefined")
    #endif()
endfunction()

function( newbase_lua_setup_fetch )
    include( FetchContent )
    FetchContent_Declare( lua_fetch
        URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
        URL_HASH SHA256=9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30
    )
    FetchContent_MakeAvailable(lua_fetch)

    add_library(lua STATIC
        ${lua_fetch_SOURCE_DIR}/src/lapi.c
        ${lua_fetch_SOURCE_DIR}/src/lcode.c
        ${lua_fetch_SOURCE_DIR}/src/lctype.c
        ${lua_fetch_SOURCE_DIR}/src/ldebug.c
        ${lua_fetch_SOURCE_DIR}/src/ldo.c
        ${lua_fetch_SOURCE_DIR}/src/ldump.c
        ${lua_fetch_SOURCE_DIR}/src/lfunc.c
        ${lua_fetch_SOURCE_DIR}/src/lgc.c
        ${lua_fetch_SOURCE_DIR}/src/llex.c
        ${lua_fetch_SOURCE_DIR}/src/lmem.c
        ${lua_fetch_SOURCE_DIR}/src/lobject.c
        ${lua_fetch_SOURCE_DIR}/src/lopcodes.c
        ${lua_fetch_SOURCE_DIR}/src/lparser.c
        ${lua_fetch_SOURCE_DIR}/src/lstate.c
        ${lua_fetch_SOURCE_DIR}/src/lstring.c
        ${lua_fetch_SOURCE_DIR}/src/ltable.c
        ${lua_fetch_SOURCE_DIR}/src/ltm.c
        ${lua_fetch_SOURCE_DIR}/src/lundump.c
        ${lua_fetch_SOURCE_DIR}/src/lvm.c
        ${lua_fetch_SOURCE_DIR}/src/lzio.c
        ${lua_fetch_SOURCE_DIR}/src/lauxlib.c
        ${lua_fetch_SOURCE_DIR}/src/lbaselib.c
        ${lua_fetch_SOURCE_DIR}/src/lcorolib.c
        ${lua_fetch_SOURCE_DIR}/src/ldblib.c
        ${lua_fetch_SOURCE_DIR}/src/liolib.c
        ${lua_fetch_SOURCE_DIR}/src/lmathlib.c
        ${lua_fetch_SOURCE_DIR}/src/loadlib.c
        ${lua_fetch_SOURCE_DIR}/src/loslib.c
        ${lua_fetch_SOURCE_DIR}/src/lstrlib.c
        ${lua_fetch_SOURCE_DIR}/src/ltablib.c
        ${lua_fetch_SOURCE_DIR}/src/lutf8lib.c
        ${lua_fetch_SOURCE_DIR}/src/linit.c
    )
    target_include_directories(lua PUBLIC ${lua_fetch_SOURCE_DIR}/src/)
    export(TARGETS lua NAMESPACE newbase FILE newbase-lua-export.cmake)
endfunction()

newbase_lua_setup_fetch()
