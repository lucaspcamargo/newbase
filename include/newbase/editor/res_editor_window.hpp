#pragma once

#include <entt/entt.hpp>
#include <string>
#include <memory>
#include <string_view>

namespace nb { class resource; }

namespace nb {

class res_editor_window
{
public:
    void open(entt::id_type type_id, entt::id_type asset_id, std::string_view title);
    void draw(bool* p_open);

private:
    entt::id_type _type_id  {};
    entt::id_type _asset_id {};
    std::string _title;
    std::shared_ptr<nb::resource> _resource;
    entt::meta_any _ref;
};

} // namespace nb
