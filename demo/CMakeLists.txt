newbase_add_executable(
    TARGET 
        newbase_demo 
    SOURCES 
        src/main.c
)
newbase_prepare_executable(TARGET newbase_demo SYSTEMS AUTO)
newbase_declare_resources(TARGET newbase_demo BUILD_SYMLINKS INDEX 
    FILES 
        ${CMAKE_SOURCE_DIR}/res/_nb_core
        res
        config.yaml
)

# TODO move this to newbase_declare_resources
if(NEWBASE_ANDROID_MAIN STREQUAL "newbase_demo")
    file(CREATE_LINK "${CMAKE_CURRENT_BINARY_DIR}/${NEWBASE_NATIVE_RES_PREFIX}" "${CMAKE_CURRENT_SOURCE_DIR}/android-project/app/assets/${NEWBASE_NATIVE_RES_PREFIX}" SYMBOLIC)
else()
    # do regular install
    install(TARGETS newbase_demo DESTINATION bin/)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/res" DESTINATION share/newbase_demo/${NEWBASE_NATIVE_RES_PREFIX}/)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/config.yaml" DESTINATION share/newbase_demo/${NEWBASE_NATIVE_RES_PREFIX}/)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${NEWBASE_NATIVE_RES_PREFIX}/index.yaml" DESTINATION share/newbase_demo/${NEWBASE_NATIVE_RES_PREFIX}/)
    install(FILES "${CMAKE_SOURCE_DIR}/res/_nb_core/icon_48.svg" DESTINATION share/icons/hicolor/scalable/apps/newbase.svg)
    if(NEWBASE_FDO_FILES)
        install(FILES fd.o/br.eng.camargo.newbase.demo.metainfo.xml DESTINATION share/metainfo/)
        install(FILES fd.o/br.eng.camargo.newbase.demo.desktop DESTINATION share/applications/)
    endif()
endif()
