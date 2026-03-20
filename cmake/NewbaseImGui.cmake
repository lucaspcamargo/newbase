
set(IMGUI_SOURCES
    vendored/imgui/imgui.cpp
    vendored/imgui/imgui_draw.cpp
    vendored/imgui/imgui_widgets.cpp
    vendored/imgui/imgui_tables.cpp
    vendored/imgui/imgui_demo.cpp  # TODO remove when needed
    vendored/imgui/backends/imgui_impl_sdl3.cpp
    vendored/imgui/backends/imgui_impl_sdlrenderer3.cpp
    vendored/imgui/backends/imgui_impl_sdlgpu3.cpp
    vendored/imgui-node-editor/imgui_node_editor.cpp
    vendored/imgui-node-editor/imgui_node_editor_api.cpp
    vendored/imgui-node-editor/crude_json.cpp
    vendored/imgui-node-editor/imgui_canvas.cpp
)

set(IMGUI_INCLUDES
    vendored/imgui
    vendored/imgui-node-editor)