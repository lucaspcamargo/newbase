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
    install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${NEWBASE_NATIVE_RES_PREFIX}" DESTINATION share/newbase_demo/)
endif()
