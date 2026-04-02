# Add editor system

newbase_add_system(NAME editor SOURCES
    ./editor.cpp
    ./about_window.cpp
    ./spdx_table.cpp
    ./res_browser.cpp
    ./res_editor_window.cpp
    ./meta_any_editor.cpp
)
newbase_commit_systems()