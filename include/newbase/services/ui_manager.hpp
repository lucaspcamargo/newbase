#pragma once
#include <functional>

namespace nb
{
    // the UI manager is the service that provides systems with a standard way to
    // create and manage UI elements, such as tool windows, debug overlays, etc.

    // there is a default implementation provided by the engine at startup, 
    // so this is always available, but it can be overridden by the editor system, for example

    class ui_manager
    {
    public:
        virtual ~ui_manager() = default;

        virtual void draw_tool_windows() = 0;

        virtual void register_tool_window(const char* name, std::function<void(bool*)> draw_fn) = 0;
        virtual void unregister_tool_window(const char* name) = 0;

        virtual bool toggle_tool_window(const char *name) = 0;
    };
}