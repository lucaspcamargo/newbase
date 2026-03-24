#include <newbase/editor/res_editor_window.hpp>
#include <newbase/editor/meta_any_editor.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/res/manager.hpp>
#include <imgui.h>
#include "IconsForkAwesome.h"

namespace nb {

void res_editor_window::open(entt::id_type type_id, entt::id_type asset_id, std::string_view title)
{
    _type_id  = type_id;
    _asset_id = asset_id;
    _resource = rman().get(type_id, asset_id);
    _ref      = {};

    // build a unique imgui window id: visible title + hidden id suffix
    _title = std::string(title) + "##resed_" + std::to_string(asset_id);

    if (_resource)
    {
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

    if (_ref)
    {
        auto meta_type = entt::resolve(_type_id);
        const rtti::type_info* info = meta_type ? meta_type.custom().operator const rtti::type_info*() : nullptr;
        const char* type_name = info ? info->identifier.operator const char*() : "?";
        ImGui::TextDisabled("%s  [id %x]", type_name, _asset_id);
        ImGui::Separator();
        draw_meta_any_editor(type_name, _ref);
    }
    else
    {
        ImGui::TextDisabled("(resource not loaded)");
    }

    ImGui::End();
}

} // namespace nb
