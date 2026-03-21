#pragma once
#include <functional>

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

        virtual void register_tool_window(const char* name, std::function<void(bool*)> draw_fn) = 0;
        virtual void unregister_tool_window(const char* name) = 0;

        virtual bool toggle_tool_window(const char *name) = 0;
    };
}