# Add steam system
newbase_add_system(NAME steam SOURCES 
    ./steam.cpp
)
newbase_commit_systems()

# setup includes and steampi lib for system 
target_include_directories(newbase_sys_steam PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../vendored/steamworks-sdk/public>
    $<INSTALL_INTERFACE:include/steamworks-sdk/public>
)
target_link_libraries(newbase_sys_steam PUBLIC steam_api)
target_link_directories(newbase_sys_steam PUBLIC ../../vendored/steamworks-sdk/redistributable_bin/linux64)