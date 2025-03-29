#include <newbase/editor/editor.h>
#include <newbase/engine.h>
#include <newbase/scene.h>
#include <newbase/reflection/contexts.h>
#include <newbase/reflection/data.h>
#include <newbase/res/manager.h>
#include <newbase/log.h>
#include <entt/entt.hpp>
#include <imgui.h>

using namespace nb;
using entt::operator""_hs;

static bool _enabled = false;

bool editor::init(int argc, char **argv) 
{
    log::info("[editor] init");
    return true;
}

bool editor::step(step_phase phase)
{
    if(phase == step_phase::POST_UPDATE && _enabled)
    {
        float fnt_size_unit = ImGui::GetFontSize();
        auto &scn = engine::instance().default_scene();
        auto &reg = scn.registry();
        ImGui::Begin("Entities");
            
            std::vector<std::string> columns;
            columns.push_back("id");
            columns.push_back("other");
            if(ImGui::BeginTable("tEntities", columns.size(), ImGuiTableFlags_ScrollY, {-FLT_MIN, -FLT_MIN}, 0))
            {
                for(auto col: columns)
                    ImGui::TableSetupColumn(col.c_str(), 0, 0);
                ImGui::TableHeadersRow();
                for(auto id: reg.view<entt::entity>())
                {
                    ImGui::TableNextColumn();
                    ImGui::Text("%x", id);
                    ImGui::TableNextRow();
                }
                ImGui::EndTable();
            }
        ImGui::End();

        ImGui::Begin("Resources");
            if(ImGui::BeginTable("##restable", 3, ImGuiTableFlags_ScrollY, {-FLT_MIN, -FLT_MIN}, 0))
            {
                ImGui::TableSetupColumn("id", ImGuiTableColumnFlags_WidthFixed, fnt_size_unit*6);
                ImGui::TableSetupColumn("path", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("size", ImGuiTableColumnFlags_WidthFixed, fnt_size_unit*6);
                ImGui::TableHeadersRow();
                for(const auto &pair: rman().descriptors())
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

    }
    return true;
}

bool editor::event(SDL_Event* evt)
{
    if(evt->type == SDL_EVENT_KEY_DOWN && evt->key.key == SDLK_GRAVE)
        _enabled = !_enabled;

    return true;
}


// RTTI metadata
extern "C" void _rtti_init_editor()
{
    entt::meta_factory<editor>{rtti::ctx_systems()}
        .type("editor"_hs)
        .custom<rtti::cstr>("editor")
        .base<nb::system>();
    entt::meta_factory<std::shared_ptr<nb::editor>>{rtti::ctx_systems()}
        .type("editor_shared"_hs)
        .ctor<&rtti::shared_ptr_builder<nb::editor>>()
        .conv<std::shared_ptr<nb::system>>();
}