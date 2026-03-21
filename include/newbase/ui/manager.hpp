#pragma once

#include <newbase/services/ui_manager.hpp>

namespace nb
{
    struct ui_manager_p;

    // this is a basic implementation of the ui_manager service
    // it is a simple implementation that allows registering and drawing of tool windows

    class ui_manager_simple : public ui_manager
    {
    public:
        ui_manager_simple();
        ~ui_manager_simple();

        bool ui_init() override;
        void ui_init_finish(float scale) override;
        void ui_new_frame(int safe_x, int safe_y, int safe_w, int safe_h) override;
        void ui_destroy() override;

        void register_tool_window(const char* name, std::function<void(bool*)> draw_fn) override;
        void unregister_tool_window(const char* name) override;
        void draw_tool_windows() override;
        void draw_perf() override;

        bool toggle_tool_window(const char *name) override;
    private:
        ui_manager_p *_d;
    };
}