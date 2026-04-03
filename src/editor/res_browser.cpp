#include <newbase/editor/res_browser.hpp>
#include <newbase/editor/resource_field_widget.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/services/ui_manager.hpp>
#include <entt/entt.hpp>
#include <imgui.h>
#include "IconsForkAwesome.h"
#include <algorithm>
#include <string>
#include <string_view>
#include <format>

namespace nb {

// Returns the first TYPE_CLASS_RESOURCE meta type whose extension list contains
// the extension of `name`, or an invalid meta_type if none match.
static entt::meta_type _resolve_file_type(std::string_view name)
{
    auto dot = name.rfind('.');
    if (dot == std::string_view::npos) return {};
    auto ext = name.substr(dot + 1);

    for (auto [id, type] : entt::resolve())
    {
        const rtti::type_info* info = type.custom();
        if (!info || info->type_class != rtti::TYPE_CLASS_RESOURCE) continue;
        const char* exts = info->data.resource.extensions;
        if (!exts) continue;

        std::string_view remaining{exts};
        while (!remaining.empty())
        {
            auto sp    = remaining.find(' ');
            auto token = (sp == std::string_view::npos) ? remaining : remaining.substr(0, sp);
            if (token == ext) return type;
            remaining  = (sp == std::string_view::npos) ? std::string_view{} : remaining.substr(sp + 1);
        }
    }
    return {};
}

const char* res_browser::_file_icon(std::string_view name)
{
    auto type = _resolve_file_type(name);
    if (!type) return ICON_FK_FILE_O;
    const rtti::type_info* info = type.custom();
    return (info && info->data.resource.editor_icon) ? info->data.resource.editor_icon : ICON_FK_FILE_O;
}

static void _try_begin_drag(std::string_view name, entt::id_type asset_id)
{
    auto type = _resolve_file_type(name);
    if (!type) return;
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
        res_drag_payload payload { asset_id, type.id() };
        ImGui::SetDragDropPayload(RES_DRAG_PAYLOAD_TYPE, &payload, sizeof(payload));
        const rtti::type_info* info = type.custom();
        const char* icon = (info && info->data.resource.editor_icon)
            ? info->data.resource.editor_icon : ICON_FK_FILE_O;
        ImGui::Text("%s %.*s", icon, (int)name.size(), name.data());
        ImGui::EndDragDropSource();
    }
}

void res_browser::_draw_tree_node(const entt::registry& reg, entt::entity e)
{
    const auto& node = reg.get<vfs_node>(e);
    bool is_file = reg.all_of<res_storage::asset_handle>(e);
    const char* icon = is_file ? _file_icon(node.name) : ICON_FK_FOLDER;

    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    if (is_file)
    {
        ImGui::TreeNodeEx((void*)(intptr_t)entt::to_integral(e),
            ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen,
            "%s %s", icon, node.name.c_str());
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
        {
            auto type = _resolve_file_type(node.name);
            if (type)
                entt::locator<ui_manager*>::value()->request_open_resource_editor(
                    type.id(), reg.get<res_storage::asset_handle>(e).id, node.name);
        }
        _try_begin_drag(node.name, reg.get<res_storage::asset_handle>(e).id);

        // Resource ID column
        ImGui::TableNextColumn();
        entt::id_type asset_id = reg.get<res_storage::asset_handle>(e).id;
        ImGui::TextDisabled("0x%08x", asset_id);

        // Resource type column
        ImGui::TableNextColumn();
        auto type = _resolve_file_type(node.name);
        if (type)
        {
            const rtti::type_info* info = type.custom();
            const char* type_name = (info && info->identifier._val) ? info->identifier.c_str() : "?";
            ImGui::TextDisabled("%s", type_name);
        }
    }
    else
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
        if (node.name.empty()) flags |= ImGuiTreeNodeFlags_DefaultOpen;
        bool open = ImGui::TreeNodeEx((void*)(intptr_t)entt::to_integral(e), flags,
            "%s %s", icon, node.name.empty() ? "/" : node.name.c_str());

        // Add empty columns for folders
        ImGui::TableNextColumn();
        ImGui::TableNextColumn();

        if (open)
        {
            for (auto child : node.children)
                _draw_tree_node(reg, child);
            ImGui::TreePop();
        }
    }
}

void res_browser::_draw_browser_grid(const entt::registry& reg, entt::entity node_e)
{
    const auto& node     = reg.get<vfs_node>(node_e);
    const auto& children = node.children;
    const int item_count = (int)children.size();

    const float font_size  = ImGui::GetFontSize();
    const float icon_sz    = _icon_size;
    const float item_w     = icon_sz;
    const float item_h     = icon_sz + font_size + 4.0f;

    const float avail_width = ImGui::GetContentRegionAvail().x;
    int   col_count  = std::max((int)(avail_width / (item_w + 10.0f)), 1);
    float spacing    = (col_count > 1) ? floorf(avail_width - item_w * col_count) / col_count : 10.0f;
    int   line_count = item_count > 0 ? (item_count + col_count - 1) / col_count : 0;

    float outer_padding      = floorf(spacing * 0.5f);
    float selectable_spacing = std::max(floorf(spacing) - 4.0f, 0.0f);
    ImVec2 item_size(item_w, item_h);
    ImVec2 item_step(item_w + spacing, item_h + spacing);

    ImGui::SetNextWindowContentSize(ImVec2(0.0f, outer_padding + line_count * item_step.y));
    if (!ImGui::BeginChild("##res_grid", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoMove))
    {
        ImGui::EndChild();
        return;
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImU32 bg_color   = ImGui::GetColorU32(IM_COL32(35, 35, 35, 220));
    const ImU32 dir_color  = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 file_color = ImGui::GetColorU32(ImGuiCol_Text);

    ImVec2 start_pos = ImGui::GetCursorScreenPos();
    start_pos = ImVec2(start_pos.x + outer_padding, start_pos.y + outer_padding);
    ImGui::SetCursorScreenPos(start_pos);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(selectable_spacing, selectable_spacing));

    ImGuiListClipper clipper;
    clipper.Begin(line_count, item_step.y);
    while (clipper.Step())
    {
        for (int line = clipper.DisplayStart; line < clipper.DisplayEnd; line++)
        {
            const int i_min = line * col_count;
            const int i_max = std::min((line + 1) * col_count, item_count);
            for (int i = i_min; i < i_max; i++)
            {
                entt::entity child_e = children[i];
                const auto& cnode  = reg.get<vfs_node>(child_e);
                bool is_file       = reg.all_of<res_storage::asset_handle>(child_e);
                const char* icon   = is_file ? _file_icon(cnode.name) : ICON_FK_FOLDER;
                entt::id_type asset_id = is_file? reg.get<res_storage::asset_handle>(child_e).id : entt::null;
                char asset_id_str[16] = {};
                if (is_file)                    
                    std::snprintf(asset_id_str, sizeof(asset_id_str), "0x%08x", asset_id);

                ImGui::PushID((int)entt::to_integral(child_e));

                ImVec2 pos(start_pos.x + (i % col_count) * item_step.x,
                           start_pos.y + line           * item_step.y);
                ImGui::SetCursorScreenPos(pos);
                ImGui::Selectable("", false, ImGuiSelectableFlags_None, item_size);

                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                {
                    if (!is_file)
                    {
                        _nav_stack.push_back(node_e);
                        _node = child_e;
                    }
                    else
                    {
                        auto type = _resolve_file_type(cnode.name);
                        if (type)
                            entt::locator<ui_manager*>::value()->request_open_resource_editor(
                                type.id(), reg.get<res_storage::asset_handle>(child_e).id, cnode.name);
                    }
                }
                if (is_file)
                    _try_begin_drag(cnode.name, reg.get<res_storage::asset_handle>(child_e).id);

                if (ImGui::IsRectVisible(item_size))
                {
                    draw_list->AddRectFilled(
                        ImVec2(pos.x, pos.y),
                        ImVec2(pos.x + icon_sz, pos.y + icon_sz),
                        bg_color);

                    ImVec2 icon_sz_vec = ImGui::CalcTextSize(icon);
                    draw_list->AddText(
                        ImVec2(pos.x + (icon_sz - icon_sz_vec.x) * 0.5f,
                               pos.y + (icon_sz - icon_sz_vec.y) * 0.5f),
                        is_file ? file_color : dir_color,
                        icon);

                    if(_show_details && is_file)
                        draw_list->AddText(
                            ImVec2(pos.x + 2, pos.y + 2),
                            is_file ? file_color : dir_color,
                            asset_id_str);

                    const char* name = cnode.name.c_str();
                    ImVec2 name_sz   = ImGui::CalcTextSize(name);
                    float  name_x    = pos.x + (icon_sz - std::min(name_sz.x, icon_sz)) * 0.5f;
                    float  name_y    = pos.y + icon_sz + 2.0f;
                    draw_list->PushClipRect(
                        ImVec2(pos.x, name_y),
                        ImVec2(pos.x + icon_sz, name_y + ImGui::GetFontSize()),
                        true);
                    draw_list->AddText(ImVec2(name_x, name_y), dir_color, name);
                    draw_list->PopClipRect();
                }

                ImGui::PopID();
            }
        }
    }
    clipper.End();
    ImGui::PopStyleVar();

    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsWindowHovered() && io.MouseWheel != 0.0f && ImGui::IsKeyDown(ImGuiMod_Ctrl))
    {
        _zoom_accum += io.MouseWheel;
        if (fabsf(_zoom_accum) >= 1.0f)
        {
            _icon_size   = std::clamp(_icon_size * powf(1.1f, (float)(int)_zoom_accum), 16.0f, 128.0f);
            _zoom_accum -= (int)_zoom_accum;
        }
    }

    ImGui::EndChild();
}

void res_browser::draw(const char* title, bool* p_open)
{
    const auto& vfs     = rman().vfs();
    const auto& vfs_reg = vfs.registry();

    // Validate/reset browser state (e.g. after vfs rebuild)
    if (!vfs_reg.valid(_node))
    {
        _node = vfs.root();
        _nav_stack.clear();
    }

    if (!ImGui::Begin(title, p_open))
    {
        ImGui::End();
        return;
    }

    // Mode toggle: tree / browser
    {
        bool active = (_mode == 0);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(ICON_FK_SITEMAP)) _mode = 0;
        if (active) ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    {
        bool active = (_mode == 1);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(ICON_FK_TH)) _mode = 1;
        if (active) ImGui::PopStyleColor();
    }

    // Breadcrumb navigation bar (browser mode only)
    if (_mode == 1)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("|");

        int crumb_count = (int)_nav_stack.size() + 1;
        for (int i = 0; i < crumb_count; i++)
        {
            entt::entity crumb_e = (i < (int)_nav_stack.size())
                ? _nav_stack[i] : _node;
            const auto& n = vfs_reg.get<vfs_node>(crumb_e);
            const char* label = n.name.empty() ? ICON_FK_ARCHIVE : n.name.c_str();

            ImGui::SameLine();
            if (i > 0) { ImGui::TextDisabled("/"); ImGui::SameLine(); }

            ImGui::PushID(i);
            if (i == crumb_count - 1)
            {
                ImGui::TextDisabled("%s", label);
            }
            else if (ImGui::SmallButton(label))
            {
                _node = crumb_e;
                _nav_stack.resize(i);
            }
            ImGui::PopID();
        }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (ImGui::Button(_show_details ? ICON_FK_EYE : ICON_FK_EYE_SLASH)) _show_details = !_show_details;

    ImGui::Separator();

    if (_mode == 0)
    {
        if (ImGui::BeginChild("##res_tree", ImVec2(-FLT_MIN, -FLT_MIN)))
        {
            if (ImGui::BeginTable("##res_tree_table", 3, ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableHeadersRow();
                
                _draw_tree_node(vfs_reg, vfs.root());
                
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();
    }
    else
    {
        _draw_browser_grid(vfs_reg, _node);
    }

    ImGui::End();
}

} // namespace nb
