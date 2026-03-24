#include <newbase/editor/editor.hpp>
#include <newbase/editor/console.hpp>
#include <newbase/engine.hpp>
#include <newbase/scene.hpp>
#include <newbase/reflection/contexts.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/log.hpp>
#include <newbase/sdl/logging_handler.hpp>
#include <newbase/utility/glm.hpp>
#include <entt/entt.hpp>
#include <imgui.h>
#include "IconsForkAwesome.h"
#include <algorithm>
#include <string>

using namespace nb;
using entt::operator""_hs;

static bool _enabled = false;
static bool _console_enabled = false;
static bool _about_enabled = false;
static bool _show_demo = false;
static int _log_observer = -1;
static console c;

static entt::entity _selected_entity  = entt::null;
static entt::id_type _selected_comp_id = 0;

// ── Resource browser state ────────────────────────────────────────────────────

static int _res_mode = 0;                           // 0 = tree, 1 = browser
static entt::entity _res_browser_node = entt::null; // currently browsed directory
static std::vector<entt::entity> _res_nav_stack;    // navigation history (parent dirs)
static float _res_icon_size = 48.0f;
static float _res_zoom_accum = 0.0f;

static const char* _res_file_icon(std::string_view name)
{
    auto dot = name.rfind('.');
    if (dot == std::string_view::npos) return ICON_FK_FILE_O;
    auto ext = name.substr(dot + 1);
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp") return ICON_FK_FILE_IMAGE_O;
    if (ext == "ogg" || ext == "wav")                                   return ICON_FK_FILE_AUDIO_O;
    if (ext == "lua")                                                    return ICON_FK_FILE_CODE_O;
    if (ext == "yaml" || ext == "yml")                                  return ICON_FK_FILE_TEXT_O;
    return ICON_FK_FILE_O;
}

static void _draw_res_tree_node(const entt::registry& reg, entt::entity e)
{
    const auto& node = reg.get<vfs_node>(e);
    bool is_file = reg.all_of<res_storage::asset_handle>(e);
    const char* icon = is_file ? _res_file_icon(node.name) : ICON_FK_FOLDER;

    if (is_file)
    {
        ImGui::TreeNodeEx((void*)(intptr_t)entt::to_integral(e),
            ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth,
            "%s %s", icon, node.name.c_str());
    }
    else
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
        if (node.name.empty()) flags |= ImGuiTreeNodeFlags_DefaultOpen;
        bool open = ImGui::TreeNodeEx((void*)(intptr_t)entt::to_integral(e), flags,
            "%s %s", icon, node.name.empty() ? "/" : node.name.c_str());
        if (open)
        {
            for (auto child : node.children)
                _draw_res_tree_node(reg, child);
            ImGui::TreePop();
        }
    }
}

static void _draw_res_browser_grid(const entt::registry& reg, entt::entity node_e)
{
    const auto& node     = reg.get<vfs_node>(node_e);
    const auto& children = node.children;
    const int item_count = (int)children.size();

    const float font_size  = ImGui::GetFontSize();
    const float icon_sz    = _res_icon_size;
    const float item_w     = icon_sz;
    const float item_h     = icon_sz + font_size + 4.0f;

    const float avail_width = ImGui::GetContentRegionAvail().x;
    int   col_count  = std::max((int)(avail_width / (item_w + 10.0f)), 1);
    float spacing    = (col_count > 1) ? floorf(avail_width - item_w * col_count) / col_count : 10.0f;
    int   line_count = item_count > 0 ? (item_count + col_count - 1) / col_count : 0;

    float outer_padding       = floorf(spacing * 0.5f);
    float selectable_spacing  = std::max(floorf(spacing) - 4.0f, 0.0f);
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
    const ImU32 file_color = ImGui::GetColorU32(ImGuiCol_TextDisabled);

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
                const char* icon   = is_file ? _res_file_icon(cnode.name) : ICON_FK_FOLDER;

                ImGui::PushID((int)entt::to_integral(child_e));

                ImVec2 pos(start_pos.x + (i % col_count) * item_step.x,
                           start_pos.y + line           * item_step.y);
                ImGui::SetCursorScreenPos(pos);
                ImGui::Selectable("", false, ImGuiSelectableFlags_None, item_size);

                // Double-click a directory to navigate into it
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && !is_file)
                {
                    _res_nav_stack.push_back(node_e);
                    _res_browser_node = child_e;
                }

                if (ImGui::IsRectVisible(item_size))
                {
                    // Icon background
                    draw_list->AddRectFilled(
                        ImVec2(pos.x, pos.y),
                        ImVec2(pos.x + icon_sz, pos.y + icon_sz),
                        bg_color);

                    // Centered icon glyph
                    ImVec2 icon_sz_vec = ImGui::CalcTextSize(icon);
                    draw_list->AddText(
                        ImVec2(pos.x + (icon_sz - icon_sz_vec.x) * 0.5f,
                               pos.y + (icon_sz - icon_sz_vec.y) * 0.5f),
                        is_file ? file_color : dir_color,
                        icon);

                    // Name label, clipped to item width
                    const char* name = cnode.name.c_str();
                    ImVec2 name_sz   = ImGui::CalcTextSize(name);
                    float  name_x    = pos.x + (icon_sz - std::min(name_sz.x, icon_sz)) * 0.5f;
                    float  name_y    = pos.y + icon_sz + 2.0f;
                    draw_list->PushClipRect(
                        ImVec2(pos.x, name_y),
                        ImVec2(pos.x + icon_sz, name_y + font_size),
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

    // Ctrl+Wheel to zoom
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsWindowHovered() && io.MouseWheel != 0.0f && ImGui::IsKeyDown(ImGuiMod_Ctrl))
    {
        _res_zoom_accum += io.MouseWheel;
        if (fabsf(_res_zoom_accum) >= 1.0f)
        {
            _res_icon_size   = std::clamp(_res_icon_size * powf(1.1f, (float)(int)_res_zoom_accum), 16.0f, 128.0f);
            _res_zoom_accum -= (int)_res_zoom_accum;
        }
    }

    ImGui::EndChild();
}

// ─────────────────────────────────────────────────────────────────────────────

static bool _draw_meta_any_editor(const char *label, entt::meta_any &ref)
{
    if (!ref) return false;
    auto ti = ref.type().info();
    bool changed = false;

    if (ti == entt::type_id<bool>()) {
        bool v = *ref.try_cast<bool>();
        if (ImGui::Checkbox(label, &v)) { ref.assign(v); changed = true; }
    } else if (ti == entt::type_id<float>()) {
        float v = *ref.try_cast<float>();
        if (ImGui::DragFloat(label, &v, 0.01f)) { ref.assign(v); changed = true; }
    } else if (ti == entt::type_id<int>()) {
        int v = *ref.try_cast<int>();
        if (ImGui::DragInt(label, &v)) { ref.assign(v); changed = true; }
    } else if (ti == entt::type_id<unsigned int>()) {
        int v = (int)*ref.try_cast<unsigned int>();
        if (ImGui::DragInt(label, &v, 1, 0)) { ref.assign((unsigned int)v); changed = true; }
    } else if (ti == entt::type_id<glm::vec2>()) {
        glm::vec2 v = *ref.try_cast<glm::vec2>();
        if (ImGui::DragFloat2(label, &v.x, 0.01f)) { ref.assign(v); changed = true; }
    } else if (ti == entt::type_id<glm::vec3>()) {
        glm::vec3 v = *ref.try_cast<glm::vec3>();
        if (ImGui::DragFloat3(label, &v.x, 0.01f)) { ref.assign(v); changed = true; }
    } else if (ti == entt::type_id<glm::vec4>()) {
        glm::vec4 v = *ref.try_cast<glm::vec4>();
        if (ImGui::DragFloat4(label, &v.x, 0.01f)) { ref.assign(v); changed = true; }
    } else if (ti == entt::type_id<glm::quat>()) {
        glm::quat v = *ref.try_cast<glm::quat>();
        if (ImGui::DragFloat4(label, &v.x, 0.001f)) { ref.assign(v); changed = true; }
    } else if (ti == entt::type_id<entt::entity>()) {
        if (auto *v = ref.try_cast<entt::entity>()) ImGui::Text("%s: %x", label, entt::to_integral(*v));
    } else if (ti == entt::type_id<std::string>()) {
        if (auto *v = ref.try_cast<std::string>()) {
            char buf[256]; strncpy(buf, v->c_str(), 255); buf[255] = 0;
            if (ImGui::InputText(label, buf, sizeof(buf))) { *v = buf; changed = true; }
        }
    } else {
        // recurse into registered data members
        auto type = ref.type();
        auto data_range = type.data();
        bool has_data = data_range.begin() != data_range.end();
        if (has_data && ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen)) {
            for (auto [did, d] : data_range) {
                const rtti::data_info *di = d.custom().operator const rtti::data_info*();
                const char *fname = di ? di->identifier.operator const char*() : "?";
                auto member = d.get(ref);
                if (_draw_meta_any_editor(fname, member))
                {
                    d.set(ref, member);
                    changed = true;
                }
            }
            ImGui::TreePop();
        } else if (!has_data) {
            ImGui::LabelText(label, "(unknown type)");
        }
    }
    return changed;
}

bool editor::init(ryml::ConstNodeRef cfg)
{
    log::info("[editor] init");
    _log_observer = log::register_observer([](int category, int prio, const char *msg){
        c.AddLog("[%s] [%s] %s", log::priority_str(static_cast<log::priority>(prio)), log::category_str(static_cast<log::category>(category)), msg);
    });

    engine::instance().debug_action_register("console toggle", [](){
        _console_enabled = !_console_enabled;
    });

    engine::instance().debug_action_register("editor toggle", [](){
        _enabled = !_enabled;
    });


    return true;
}

bool editor::step(step_phase phase)
{
    if(phase == step_phase::POST_UPDATE && _console_enabled)
    {
        c.Draw(ICON_FK_TERMINAL " Console", &_console_enabled);
    }

    if(phase == step_phase::POST_UPDATE && _enabled)
    {
        float fnt_size_unit = ImGui::GetFontSize();
        auto &scn = engine::instance().default_scene();
        auto &reg = scn.registry();

        _draw_main_menu();

        ImGui::Begin(ICON_FK_TABLE " Entities");

            std::vector<std::string> columns;
            columns.push_back("id");
            columns.push_back("components");
            if(ImGui::BeginTable("##entitiestable", columns.size(), ImGuiTableFlags_ScrollY, {-FLT_MIN, -FLT_MIN}, 0))
            {
                for(auto col: columns)
                ImGui::TableSetupColumn(col.c_str(), 0, 0);
                ImGui::TableHeadersRow();
                for(auto id: reg.view<entt::entity>())
                {
                    ImGui::PushID(static_cast<int>(id));
                    ImGui::TableNextColumn();
                    ImGui::Text("%x", id);
                    ImGui::TableNextColumn();
                    // get list of components
                    std::vector<entt::id_type> components;
                    std::string icons;
                    for(auto&& curr : reg.storage())
                    {
                        if(auto& storage = curr.second; storage.contains(id))
                        {
                            entt::id_type comp_id = curr.first;
                            components.push_back(comp_id);
                            auto comp_type = entt::resolve(comp_id);
                            if(comp_type.info() == entt::type_id<void>())
                            {
                                continue;
                            }
                            rtti::type_info *info = comp_type.custom();
                            if((!info) || (info->type_class != rtti::TYPE_CLASS_COMPONENT))
                            {
                                continue;
                            }
                            ImGui::SameLine();
                            bool selected = (_selected_entity == id && _selected_comp_id == comp_id);
                            if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                            if (ImGui::Button(info->data.component.editor_icon ? info->data.component.editor_icon : "??"))
                            {
                                _selected_entity  = id;
                                _selected_comp_id = comp_id;
                            }
                            if (selected) ImGui::PopStyleColor();
                        }
                    }
                    ImGui::PopID();
                    ImGui::TableNextRow();
                }
                ImGui::EndTable();
            }
        ImGui::End();

        ImGui::Begin(ICON_FK_PENCIL " Properties");
        if (_selected_entity != entt::null && _selected_comp_id != 0)
        {
            auto *storage = reg.storage(_selected_comp_id);
            if (storage && storage->contains(_selected_entity))
            {
                auto comp_type = entt::resolve(_selected_comp_id);
                rtti::type_info *info = comp_type.custom();
                ImGui::TextDisabled("%s  [entity %x]",
                    info ? info->identifier.operator const char*() : "?",
                    entt::to_integral(_selected_entity));
                ImGui::Separator();
                void * void_val = storage->value(_selected_entity);
                auto ref = comp_type.from_void(void_val);
                if(_draw_meta_any_editor(info ? info->identifier.operator const char*() : "?", ref))
                {
                    if (info && info->data.component.notify)
                        info->data.component.notify(reg, _selected_entity, void_val);
                }
            }
            else
            {
                _selected_entity  = entt::null;
                _selected_comp_id = 0;
            }
        }
        else
        {
            ImGui::TextDisabled("(select a component)");
        }
        ImGui::End();

        ImGui::Begin(ICON_FK_ARCHIVE " Resources");
        {
            const auto& vfs = rman().vfs();
            const auto& vfs_reg = vfs.registry();

            // Validate/reset browser state (e.g. after vfs rebuild)
            if (!vfs_reg.valid(_res_browser_node))
            {
                _res_browser_node = vfs.root();
                _res_nav_stack.clear();
            }

            // Mode toggle: tree / browser
            {
                bool active = (_res_mode == 0);
                if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::Button(ICON_FK_SITEMAP)) _res_mode = 0;
                if (active) ImGui::PopStyleColor();
            }
            ImGui::SameLine();
            {
                bool active = (_res_mode == 1);
                if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::Button(ICON_FK_TH)) _res_mode = 1;
                if (active) ImGui::PopStyleColor();
            }

            // Breadcrumb navigation bar (browser mode only)
            if (_res_mode == 1)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("|");

                int crumb_count = (int)_res_nav_stack.size() + 1;
                for (int i = 0; i < crumb_count; i++)
                {
                    entt::entity crumb_e = (i < (int)_res_nav_stack.size())
                        ? _res_nav_stack[i] : _res_browser_node;
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
                        _res_browser_node = crumb_e;
                        _res_nav_stack.resize(i);
                    }
                    ImGui::PopID();
                }
            }

            ImGui::Separator();

            if (_res_mode == 0)
            {
                if (ImGui::BeginChild("##res_tree", ImVec2(-FLT_MIN, -FLT_MIN)))
                    _draw_res_tree_node(vfs_reg, vfs.root());
                ImGui::EndChild();
            }
            else
            {
                _draw_res_browser_grid(vfs_reg, _res_browser_node);
            }
        }
        ImGui::End();

        if(_show_demo)
        {
            ImGui::ShowDemoWindow(&_show_demo);
        }

        if(_about_enabled)
        {
            ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

            if(ImGui::Begin(ICON_FK_INFO_CIRCLE " About", &_about_enabled, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("newbase Engine");
                ImGui::Separator();
                ImGui::Text("Version %s", NEWBASE_VERSION);
                ImGui::Text("Authors: %s", NEWBASE_AUTHORS);
                ImGui::Text("%s", NEWBASE_COPYRIGHT);
                ImGui::Separator();
                ImGui::Text("A little game/multimedia engine of mine.");
                ImGui::TextLinkOpenURL("More info...", NEWBASE_URL);
            }
            ImGui::End();
        }

    }
    return true;
}

bool editor::event(SDL_Event* evt)
{
    return true;
}

void editor::_draw_main_menu()
{
    // draw global menu bar
    if(ImGui::BeginMainMenuBar())
    {
        if(ImGui::BeginMenu("File"))
        {
            if(ImGui::MenuItem("Exit", "Esc"))
            {
                engine::instance().request_exit();
            }
            ImGui::EndMenu();
        }

        if(ImGui::BeginMenu("View"))
        {
            if(ImGui::MenuItem("Console", "F10", _console_enabled))
            {
                _console_enabled = !_console_enabled;
            }
            if(ImGui::MenuItem("Editor", "F9", _enabled))
            {
                _enabled = !_enabled;
            }
            ImGui::EndMenu();
        }

        if(ImGui::BeginMenu("Help"))
        {
            if(ImGui::MenuItem("ImGui demo", NULL, _show_demo))
            {
                _show_demo = !_show_demo;
            }

            if(ImGui::MenuItem("About"))
            {
                _about_enabled = true;
            }
            ImGui::EndMenu();
        }

        // on the right edge of the menu bar, add a combo box for the currently selected scene
            ImGui::Separator();
            if (ImGui::BeginTabBar("##TabBar"))
            {
                if (ImGui::BeginTabItem("Blah"))
                    ImGui::EndTabItem();
                if (ImGui::BeginTabItem("Blih"))
                    ImGui::EndTabItem();
                if (ImGui::BeginTabItem("Blou"))
                    ImGui::EndTabItem();
                ImGui::EndTabBar();
            }

        ImGui::EndMainMenuBar();
    }
}

// RTTI metadata
extern "C" void _rtti_init_editor()
{
    entt::meta_factory<editor>{}
        .type("editor"_hs)
        .custom<rtti::type_info>(rtti::type_info{"editor", rtti::TYPE_CLASS_SYSTEM})
        .base<nb::system>();

    entt::meta_factory<std::shared_ptr<nb::editor>>{rtti::ctx_systems()}
        .type("editor_shared"_hs)
        .ctor<&rtti::shared_ptr_builder<nb::editor>>()
        .conv<std::shared_ptr<nb::system>>();
}
