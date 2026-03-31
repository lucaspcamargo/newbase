newbase_add_executable(
    TARGET 
        newbase_demo 
    SOURCES 
        src/main.c
)
newbase_prepare_executable(TARGET newbase_demo SYSTEMS AUTO)
#if(NOT WIN32)
set(BUILD_SYMLINKS BUILD_SYMLINKS)
#endif()
newbase_declare_resources(TARGET newbase_demo ${BUILD_SYMLINKS} INDEX 
    FILES
        res
        config.yaml
)

if(NEWBASE_SGDK)
    # we have sgdk support, let's add the game target for use in the demo
    newbase_sgdk_add_game( TARGET newbase_demo_sgdk_hello
        MAIN
        ${NEWBASE_SGDK_PATH}/sample/basics/hello-world/src/main.c
        SOURCES
        ${NEWBASE_SGDK_PATH}/sample/basics/hello-world/src/rom_header.c
        )
        
    target_link_libraries(newbase_demo PRIVATE newbase_demo_sgdk_hello)
endif(NEWBASE_SGDK)

# TODO move this to newbase_declare_resources
if(NEWBASE_ANDROID_MAIN STREQUAL "newbase_demo")
    file(CREATE_LINK "${CMAKE_CURRENT_BINARY_DIR}/${NEWBASE_NATIVE_RES_PREFIX}" "${CMAKE_CURRENT_SOURCE_DIR}/android-project/app/assets/${NEWBASE_NATIVE_RES_PREFIX}" SYMBOLIC)
else()
    # do regular install
    install(TARGETS newbase_demo DESTINATION bin/)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/res" DESTINATION share/newbase_demo/${NEWBASE_NATIVE_RES_PREFIX}/)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/config.yaml" DESTINATION share/newbase_demo/${NEWBASE_NATIVE_RES_PREFIX}/)
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/res/_nb_core" DESTINATION share/newbase_demo/${NEWBASE_NATIVE_RES_PREFIX}/)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${NEWBASE_NATIVE_RES_PREFIX}/index.yaml" DESTINATION share/newbase_demo/${NEWBASE_NATIVE_RES_PREFIX}/)
    install(FILES "${CMAKE_SOURCE_DIR}/res/_nb_core/icon.svg" DESTINATION share/icons/hicolor/scalable/apps/ RENAME br.eng.camargo.newbase.demo.svg)
    if(NEWBASE_FDO_FILES)
        install(FILES fd.o/br.eng.camargo.newbase.demo.metainfo.xml DESTINATION share/metainfo/)
        install(FILES fd.o/br.eng.camargo.newbase.demo.desktop DESTINATION share/applications/)
    endif()
endif()
