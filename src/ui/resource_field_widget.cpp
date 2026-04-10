#include <newbase/ui/resource_field_widget.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/services/ui_manager.hpp>
#include <newbase/log.hpp>
#include <imgui.h>
#include "IconsForkAwesome.h"
#include <entt/entt.hpp>
#include <cstdio>

namespace nb {

bool draw_resource_field(const char* label, entt::id_type res_type_id,
                         std::shared_ptr<nb::resource>& ptr)
{
    bool changed = false;

    // resolve icon from the resource type's rtti
    const char* icon = "";
    auto res_meta = entt::resolve(res_type_id);
    if (res_meta)
    {
        const rtti::type_info* res_rtti = res_meta.custom().operator const rtti::type_info*();
        if (res_rtti && res_rtti->type_class == rtti::TYPE_CLASS_RESOURCE
                     && res_rtti->data.resource.editor_icon)
            icon = res_rtti->data.resource.editor_icon;
    }

    char buf[256];
    if (ptr)
    {
        entt::id_type asset_id = ptr->id();
        const char* name = nullptr;
        auto& handles = rman().handles();
        auto it = handles.find(asset_id);
        if (it != handles.end() && !it->second.name.empty())
            name = it->second.name.c_str();

        if (name)
            snprintf(buf, sizeof(buf), "%s %s  [%x]", icon, name, asset_id);
        else
            snprintf(buf, sizeof(buf), "%s [%x]", icon, asset_id);
    }
    else
    {
        snprintf(buf, sizeof(buf), "%s (none)", icon);
    }

    // layout: shrink input to leave room for the pencil + × buttons
    float btn_w = ImGui::GetFrameHeight();
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - (btn_w + spacing) * 2);

    ImGui::PushID(label);

    ImGui::InputText("##v", buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Drag a resource here, or double-click to open editor");
    bool open_editor = ptr && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);

    // drop target on the input field
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(RES_DRAG_PAYLOAD_TYPE))
        {
            const res_drag_payload* p = static_cast<const res_drag_payload*>(payload->Data);
            if (p->res_type_id == res_type_id)
            {
                ptr = rman().get(res_type_id, p->asset_id);
                changed = true;
            }
            else
            {
                log::warn("Resource type mismatch: %x!=%x", p->res_type_id, res_type_id);
            }
        }
        ImGui::EndDragDropTarget();
    }

    // pencil button — opens the resource editor
    ImGui::SameLine(0, spacing);
    ImGui::BeginDisabled(!ptr);
    if (ImGui::Button(ICON_FK_PENCIL, {btn_w, 0}))
        open_editor = true;
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Edit resource");

    if (open_editor)
    {
        entt::id_type asset_id = ptr->id();
        const char* name = "";
        auto& handles = rman().handles();
        auto it = handles.find(asset_id);
        if (it != handles.end() && !it->second.name.empty())
            name = it->second.name.c_str();
        entt::locator<ui_manager*>::value()->request_open_resource_editor(res_type_id, asset_id, name);
    }

    // × button
    ImGui::SameLine(0, spacing);
    if (ImGui::Button("x", {btn_w, 0}) && ptr)
    {
        ptr = nullptr;
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Clear reference");

    ImGui::PopID();

    // label
    ImGui::SameLine(0, spacing);
    ImGui::TextUnformatted(label);

    return changed;
}

} // namespace nb
