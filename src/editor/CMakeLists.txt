# Add editor system

newbase_add_system(NAME editor SOURCES
    ./editor.cpp
    ./about_window.cpp
    ./rtti_window.cpp
    ./hash_window.cpp
    ./render_layers_window.cpp
    ./spdx_table.cpp
    ./res_browser.cpp
    ./res_editor_window.cpp
    ./texture_editor_widget.cpp
    ./audio_editor_widget.cpp
    ./text_editor_widget.cpp
    ./graphplan_editor_widget.cpp
)
newbase_commit_systems()