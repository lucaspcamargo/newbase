#include <newbase/editor/editor.hpp>
#include <newbase/editor/about_window.hpp>
#include <newbase/editor/rtti_window.hpp>
#include <newbase/editor/hash_window.hpp>
#include <newbase/editor/console.hpp>
#include <newbase/editor/res_browser.hpp>
#include <newbase/editor/res_editor_window.hpp>
#include <newbase/editor/meta_any_editor.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/engine.hpp>
#include <newbase/scene.hpp>
#include <newbase/reflection/contexts.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/services/ui_manager.hpp>
#include <newbase/log.hpp>
#include <newbase/sdl/logging_handler.hpp>
#include <newbase/utility/glm.hpp>
#include <newbase/ui/imgui_style.hpp>
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
static bool _rtti_enabled = false;
static bool _hash_enabled = false;
static bool _show_demo = false;
static int _log_observer = -1;
static console c;
static nb::res_browser _res_browser;
static std::vector<nb::res_editor_window> _res_editors;
static nb::about_window _about;
static nb::rtti_window _rtti;
static nb::hash_window _hash;

static entt::entity _selected_entity = entt::null;

bool editor::init(ryml::ConstNodeRef cfg)
{
    log::info("[editor] init");
    _log_observer = log::register_observer([](int category, int prio, const char *msg){
        c.AddLog("[%s] [%s] %s", log::priority_str(static_cast<log::priority>(prio)), log::category_str(static_cast<log::category>(category)), msg);
    });

    engine::instance().debug_action_register("Console", [](){
        _console_enabled = !_console_enabled;
    });

    engine::instance().debug_action_register("Editor", [](){
        _enabled = !_enabled;
        engine::instance().set_paused(_enabled);
    });

    auto open_res_editor = [](entt::id_type type_id, entt::id_type asset_id, std::string_view name) {
        _res_editors.emplace_back().open(type_id, asset_id, name);
    };

    ui_manager* ui_mgr = entt::locator<ui_manager*>::value();
    ui_mgr->register_open_resource_editor_callback(open_res_editor);

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
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::PushID(static_cast<int>(entt::to_integral(id)));

                    bool selected = (_selected_entity == id);
                    if (ImGui::Selectable("##row", selected,
                        ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                        ImVec2(0.0f, 0.0f)))
                    {
                        _selected_entity = id;
                    }
                    ImGui::SameLine();
                    ImGui::Text("%x", entt::to_integral(id));

                    ImGui::TableNextColumn();
                    for(auto&& curr : reg.storage())
                    {
                        if(auto& storage = curr.second; storage.contains(id))
                        {
                            auto comp_type = entt::resolve(curr.first);
                            if(comp_type.info() == entt::type_id<void>()) continue;
                            rtti::type_info *info = comp_type.custom();
                            if(!info || info->type_class != rtti::TYPE_CLASS_COMPONENT) continue;
                            ImGui::SameLine();
                            ImGui::TextUnformatted(info->data.component.editor_icon ? info->data.component.editor_icon : "?");
                        }
                    }

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        ImGui::End();

        ImGui::Begin(ICON_FK_PENCIL " Properties");
        if (_selected_entity != entt::null && reg.valid(_selected_entity))
        {
            ImGui::TextDisabled("entity %x", entt::to_integral(_selected_entity));
            ImGui::Separator();
            for(auto&& curr : reg.storage())
            {
                auto& storage = curr.second;
                if(!storage.contains(_selected_entity)) continue;
                auto comp_type = entt::resolve(curr.first);
                if(comp_type.info() == entt::type_id<void>()) continue;
                rtti::type_info *info = comp_type.custom();
                if(!info || info->type_class != rtti::TYPE_CLASS_COMPONENT) continue;

                const char *comp_name = info->identifier.operator const char*();
                void *void_val = storage.value(_selected_entity);
                auto ref = comp_type.from_void(void_val);

                ImGui::PushID(static_cast<int>(curr.first));
                if(ImGui::CollapsingHeader(comp_name, ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if(draw_meta_any_editor(comp_name, ref))
                    {
                        if(info->data.component.notify)
                            info->data.component.notify(reg, _selected_entity, void_val);
                    }
                }
                ImGui::PopID();
            }
        }
        else
        {
            _selected_entity = entt::null;
            ImGui::TextDisabled("(select an entity)");
        }
        ImGui::End();

        _res_browser.draw(ICON_FK_ARCHIVE " Resources");

        // we use index-based iteration here because a resource could potentially
        // open another resource editor window
        for (size_t i = 0, n = _res_editors.size(); i < n; )
        {
            bool open = true;
            _res_editors[i].draw(&open);
            if (!open) { _res_editors.erase(_res_editors.begin() + i); --n; }
            else ++i;
        }

        if(_show_demo)
        {
            ImGui::ShowDemoWindow(&_show_demo);
        }

        if(_about_enabled)
            _about.draw(&_about_enabled);

        if(_rtti_enabled)
            _rtti.draw(&_rtti_enabled);

        if(_hash_enabled)
            _hash.draw(&_hash_enabled);

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

            ImGui::Separator();

            if(ImGui::BeginMenu("Theme"))
            {
                if(ImGui::MenuItem("Default"))
                    imgui_style_setup();
                if(ImGui::MenuItem("Dark"))
                    ImGui::StyleColorsDark();
                if(ImGui::MenuItem("Light"))
                    ImGui::StyleColorsLight();
                if(ImGui::MenuItem("Classic"))
                    ImGui::StyleColorsClassic();
                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }

        if(ImGui::BeginMenu("Help"))
        {
            if(ImGui::MenuItem("ImGui demo", NULL, _show_demo))
            {
                _show_demo = !_show_demo;
            }

            if(ImGui::MenuItem("RTTI Info"))
            {
                _rtti_enabled = true;
            }

            if(ImGui::MenuItem("Hash Calculator"))
            {
                _hash_enabled = true;
            }

            if(ImGui::MenuItem("About"))
            {
                _about_enabled = true;
            }
            ImGui::EndMenu();
        }

            /* TABS IN MENU TEST
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
            }*/

        // right-aligned play/pause button
        float btn_w = ImGui::GetFrameHeight() * 2.0f;
        ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - btn_w);
        if (engine::instance().is_paused())
        {
            if (ImGui::Button(ICON_FK_PLAY " Play", {btn_w, 0}))
                engine::instance().set_paused(false);
        }
        else
        {
            if (ImGui::Button(ICON_FK_PAUSE " Pause", {btn_w, 0}))
                engine::instance().set_paused(true);
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
