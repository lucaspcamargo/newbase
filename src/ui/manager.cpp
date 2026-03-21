#include <newbase/ui/manager.hpp>
#include <newbase/log.hpp>
#include <unordered_map>
#include <string>

using namespace nb;

struct tool_window_data
{
    std::string name {};
    std::function<void(bool*)> draw_fn {};
    bool enabled = false;
};

struct nb::ui_manager_p
{
    bool editor_mode = false;
    std::unordered_map<std::string, tool_window_data> tool_windows;
};


ui_manager_simple::ui_manager_simple()
{
    _d = new ui_manager_p();
    log::info("[ui_manager] initialized");
}

ui_manager_simple::~ui_manager_simple()
{
    delete _d;
    log::info("[ui_manager] destroyed");
}

void ui_manager_simple::register_tool_window(const char* name, std::function<void(bool*)> draw_fn)
{
    _d->tool_windows[name] = {name, draw_fn, false};
    log::info("[ui_manager] registered tool window '%s'", name);
}

void ui_manager_simple::unregister_tool_window(const char* name)
{
    _d->tool_windows.erase(name);
    log::info("[ui_manager] unregistered tool window '%s'", name);
}

void ui_manager_simple::draw_tool_windows()
{
    for(auto& pair: _d->tool_windows)
    {
        if(!pair.second.enabled)
            continue;
        const std::string& name = pair.first;
        auto& draw_fn = pair.second.draw_fn;
        draw_fn(&(pair.second.enabled));
    }
}

bool ui_manager_simple::toggle_tool_window(const char *name)
{
    auto it = _d->tool_windows.find(name);
    if(it != _d->tool_windows.end())
    {
        if(it->second.enabled)
        {
            log::info("[ui_manager] toggling tool window '%s' off", name);
            it->second.enabled = false;
        }
        else
        {
            log::info("[ui_manager] toggling tool window '%s' on", name);
            it->second.enabled = true;
        }
        return true;
    }
    return false;
}