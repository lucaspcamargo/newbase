#include <newbase/editor/editor.hpp>
#include <newbase/editor/console.hpp>
#include <newbase/engine.hpp>
#include <newbase/scene.hpp>
#include <newbase/reflection/contexts.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/log.hpp>
#include <newbase/sdl/logging_handler.hpp>
#include <entt/entt.hpp>
#include <imgui.h>
#include "IconsForkAwesome.h"

using namespace nb;
using entt::operator""_hs;

static bool _enabled = false;
static bool _console_enabled = false;
static bool _about_enabled = false;
static int _log_observer = -1;
static console c;

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

        // draw global menu bar
        if(ImGui::BeginMainMenuBar())
        {
            if(ImGui::BeginMenu("File"))
            {
                if(ImGui::MenuItem("Exit"))
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
                if(ImGui::MenuItem("About"))
                {
                    _about_enabled = true;
                }
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

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
                            ImGui::Button(info->data.component.editor_icon? info->data.component.editor_icon: "??");
                        }
                    }
                    ImGui::PopID();
                    ImGui::TableNextRow();
                }
                ImGui::EndTable();
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
                ImGui::Text("A little game engine of mine.");
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