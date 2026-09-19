
set(IMGUI_SOURCES
    vendored/imgui/imgui.cpp
    vendored/imgui/imgui_draw.cpp
    vendored/imgui/imgui_widgets.cpp
    vendored/imgui/imgui_tables.cpp
    vendored/imgui/imgui_demo.cpp  # TODO remove when needed
    vendored/imgui-node-editor/imgui_node_editor.cpp
    vendored/imgui-node-editor/imgui_node_editor_api.cpp
    vendored/imgui-node-editor/crude_json.cpp
    vendored/imgui-node-editor/imgui_canvas.cpp
    vendored/ImGuizmo/src/ImGuizmo.cpp
)

set(IMGUI_INCLUDES
    vendored/imgui
    vendored/imgui-node-editor
    vendored/ImGuizmo/src)
