#pragma once

#include <entt/entt.hpp>
#include <functional>
#include <vector>
#include <string_view>

namespace nb {

class res_browser
{
public:
    // Called when the user double-clicks a file. Arguments: type_id, asset_id, name.
    std::function<void(entt::id_type, entt::id_type, std::string_view)> on_open_file;

    void draw(const char* title, bool* p_open = nullptr);

private:
    int _mode          = 0;
    entt::entity _node = entt::null;
    std::vector<entt::entity> _nav_stack;
    float _icon_size   = 48.0f;
    float _zoom_accum  = 0.0f;

    static const char* _file_icon(std::string_view name);
    void _draw_tree_node(const entt::registry& reg, entt::entity e);
    void _draw_browser_grid(const entt::registry& reg, entt::entity node_e);
};

} // namespace nb
