#pragma once

#include <newbase/layer.hpp>
#include <newbase/utility/glm.hpp>

#include <functional>
#include <entt/entt.hpp>
#include <string_view>

namespace nb
{
    // the UI manager is the service that provides systems with a standard way to
    // create and manage UI elements, such as tool windows, debug overlays, etc.

    // there is a default implementation provided by the engine at startup, 
    // so this is always available, but it could be overridden by the editor system, for example

    class ui_manager
    {
    public:
        virtual ~ui_manager() = default;

        virtual bool ui_init() = 0; // before a backend and renderer are setup
        virtual void ui_init_finish(float scale) = 0; // after a backend and renderer are setup
        virtual void ui_new_frame(int safe_x, int safe_y, int safe_w, int safe_h) = 0;  // start a new GUI frame, within a given work area
        virtual void ui_destroy() = 0; // after renderer and backend are destroyed

        virtual void draw_tool_windows() = 0;
        virtual void draw_perf() = 0;
        virtual void draw_overlays() = 0;

        // Overlay callbacks — drawn after all tool windows, on the foreground draw list.
        // "layer" overlays are suitable for debug wireframes, physics shapes, editor gizmos, etc.
        // Receives the render layer to draw with, and a viewport in gui space
        // Always use the ui viewport instead of the layer's viewport
        using layer_overlay_fn = std::function<void(const render_layer &rl, glm::vec4 ui_vp)>;
        virtual void register_layer_overlay(const char* name, layer_overlay_fn fn) = 0;
        virtual void unregister_layer_overlay(const char* name) = 0;
        // "ui" overlays are suitable for interactive elements on the main UI viewport,
        // that have no relation to specific render layers. The callback is invoked
        // unconditionally, after the "layer" overlays
        using ui_overlay_fn = std::function<void(glm::vec4 ui_vp)>;
        virtual void register_ui_overlay(const char* name, ui_overlay_fn fn) = 0;
        virtual void unregister_ui_overlay(const char* name) = 0;

        // Returns the ImGuiID of the root dockspace, or 0 if not available.
        virtual unsigned int dockspace_id() const { return 0; }

        // Tool winddow management
        virtual void register_tool_window(const char* name, std::function<void(bool*)> draw_fn) = 0;
        virtual void unregister_tool_window(const char* name) = 0;
        virtual bool toggle_tool_window(const char *name) = 0;

        // Viewport management
        virtual glm::ivec4 update_viewports() = 0;  // returns central ui viewport in pixel coords
        virtual glm::vec4 central_viewport(bool pixel_coords = false) = 0; // return in ui coords by default

        // Resource editor integration.
        // A system that can display a resource editor (e.g. the editor system) registers a
        // callback here. Callers (resource field widget, resource browser, etc.) invoke
        // request_open_resource_editor without needing to know who handles it.
        using open_resource_editor_fn = std::function<void(entt::id_type type_id, entt::id_type asset_id, std::string_view name)>;
        virtual void register_open_resource_editor_callback(open_resource_editor_fn fn) = 0;
        virtual void request_open_resource_editor(entt::id_type type_id, entt::id_type asset_id, std::string_view name) = 0;
    };
}
