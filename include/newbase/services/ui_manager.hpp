#pragma once
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
        virtual void draw_perf() = 0;  // maybe replace with generic draw_overlays, move physiscs2d overlay to this new system

        // Returns the ImGuiID of the root dockspace, or 0 if not available.
        virtual unsigned int dockspace_id() const { return 0; }

        virtual void register_tool_window(const char* name, std::function<void(bool*)> draw_fn) = 0;
        virtual void unregister_tool_window(const char* name) = 0;

        virtual bool toggle_tool_window(const char *name) = 0;

        // Resource editor integration.
        // A system that can display a resource editor (e.g. the editor system) registers a
        // callback here. Callers (resource field widget, resource browser, etc.) invoke
        // request_open_resource_editor without needing to know who handles it.
        using open_resource_editor_fn = std::function<void(entt::id_type type_id, entt::id_type asset_id, std::string_view name)>;
        virtual void register_open_resource_editor_callback(open_resource_editor_fn fn) = 0;
        virtual void request_open_resource_editor(entt::id_type type_id, entt::id_type asset_id, std::string_view name) = 0;
    };
}