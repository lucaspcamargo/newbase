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
                    d.set(ref, member);
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
                auto ref = comp_type.from_void(storage->value(_selected_entity));
                for (auto [did, d] : comp_type.data())
                {
                    const rtti::data_info *di = d.custom().operator const rtti::data_info*();
                    const char *fname = di ? di->identifier.operator const char*() : "?";
                    auto member = d.get(ref);
                    if (_draw_meta_any_editor(fname, member))
                        d.set(ref, member);
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
            if(ImGui::BeginTable("##restable", 3, ImGuiTableFlags_ScrollY, {-FLT_MIN, -FLT_MIN}, 0))
            {
                ImGui::TableSetupColumn("id", ImGuiTableColumnFlags_WidthFixed, fnt_size_unit*6);
                ImGui::TableSetupColumn("path", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("size", ImGuiTableColumnFlags_WidthFixed, fnt_size_unit*6);
                ImGui::TableHeadersRow();
                for(const auto &pair: rman().handles())
                {
                    ImGui::TableNextColumn();
                    ImGui::Text("%x", pair.first);
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", pair.second.path.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%zu", pair.second.size);
                    ImGui::TableNextRow();
                }
                ImGui::EndTable();
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