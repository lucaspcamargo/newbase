#pragma once
#include <functional>

namespace nb
{
    class ui_manager
    {
    public:
        virtual ~ui_manager() = default;

        virtual void register_tool_window(const char* name, std::function<void(bool*)> draw_fn) = 0;
        virtual void unregister_tool_window(const char* name) = 0;
    };
}