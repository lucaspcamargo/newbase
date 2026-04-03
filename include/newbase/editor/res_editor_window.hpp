#pragma once

#include <entt/entt.hpp>
#include <string>
#include <memory>
#include <string_view>

namespace nb { class resource; class texture_editor_widget; class audio_editor_widget; class text_editor_widget; }

namespace nb {

class res_editor_window
{
public:
    res_editor_window();
    ~res_editor_window();
    res_editor_window(res_editor_window&&) noexcept;
    res_editor_window& operator=(res_editor_window&&) noexcept;

    void open(entt::id_type type_id, entt::id_type asset_id, std::string_view title);
    void draw(bool* p_open);

private:
    entt::id_type _type_id  {};
    entt::id_type _asset_id {};
    std::string _title;
    std::shared_ptr<nb::resource> _resource;
    entt::meta_any _ref;
    std::unique_ptr<texture_editor_widget> _tex_widget;
    std::unique_ptr<audio_editor_widget>   _audio_widget;
    std::unique_ptr<text_editor_widget>    _text_widget;
};

} // namespace nb
