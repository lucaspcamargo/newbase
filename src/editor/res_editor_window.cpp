#include <newbase/editor/res_editor_window.hpp>
#include <newbase/editor/meta_any_editor.hpp>
#include <newbase/editor/texture_editor_widget.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/res/texture.hpp>
#include <imgui.h>
#include "IconsForkAwesome.h"

using entt::operator""_hs;

namespace nb {

res_editor_window::res_editor_window() = default;
res_editor_window::~res_editor_window() = default;
res_editor_window::res_editor_window(res_editor_window&&) noexcept = default;
res_editor_window& res_editor_window::operator=(res_editor_window&&) noexcept = default;

void res_editor_window::open(entt::id_type type_id, entt::id_type asset_id, std::string_view title)
{
    _type_id  = type_id;
    _asset_id = asset_id;
    _resource = rman().get(type_id, asset_id);
    _ref      = {};
    _tex_widget.reset();

    // build a unique imgui window id: visible title + hidden id suffix
    _title = std::string(title) + "##resed_" + std::to_string(asset_id);

    if (_resource)
    {
        // Texture: open the dedicated paint widget
        if (_resource->type_id() == "rtexture"_hs.value())
        {
            auto* rt = static_cast<rtexture*>(_resource.get());
            // Surface may have been freed after GPU upload — reload it on demand.
            if (!rt->surf && rt->reload_surface)
                rt->surf = rt->reload_surface(rt->id());
            if (rt->surf)
            {
                _tex_widget = std::make_unique<texture_editor_widget>();
                _tex_widget->open(rt->surf);
            }
        }
        // Always build a meta_any too (shown as fallback or alongside)
        auto meta_type = entt::resolve(type_id);
        if (meta_type)
            _ref = meta_type.from_void(dynamic_cast<void*>(_resource.get()));
    }
}

void res_editor_window::draw(bool* p_open)
{
    if (!ImGui::Begin(_title.c_str(), p_open, ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::End();
        return;
    }

    auto meta_type = entt::resolve(_type_id);
    const rtti::type_info* info = meta_type ? meta_type.custom().operator const rtti::type_info*() : nullptr;
    const char* type_name = info ? info->identifier.operator const char*() : "?";
    ImGui::TextDisabled("%s  [id %x]", type_name, _asset_id);
    ImGui::Separator();

    if (_tex_widget)
    {
        if (ImGui::Button(ICON_FK_CHECK " Apply"))
            _tex_widget->apply(_resource.get());
        ImGui::SameLine();
        if (ImGui::Button(ICON_FK_FLOPPY_O " Save"))
        {
            _tex_widget->apply(_resource.get());
            rman().save_resource(_resource.get());
        }
        ImGui::Separator();
        _tex_widget->draw();
    }
    else if (_ref)
        draw_meta_any_editor(type_name, _ref);
    else
        ImGui::TextDisabled("(resource not loaded)");

    ImGui::End();
}

} // namespace nb
