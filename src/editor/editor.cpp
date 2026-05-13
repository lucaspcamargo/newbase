#include <newbase/editor/editor.hpp>
#include <newbase/editor/about_window.hpp>
#include <newbase/editor/rtti_window.hpp>
#include <newbase/editor/hash_window.hpp>
#include <newbase/editor/render_layers_window.hpp>
#include <newbase/editor/console.hpp>
#include <newbase/editor/res_browser.hpp>
#include <newbase/editor/res_editor_window.hpp>
#include <newbase/editor/meta_any_editor.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/engine.hpp>
#include <newbase/scene.hpp>
#include <newbase/layer.hpp>
#include <newbase/reflection/contexts.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/services/ui_manager.hpp>
#include <newbase/services/renderer_service.hpp>
#include <newbase/log.hpp>
#include <newbase/sdl/logging_handler.hpp>
#include <SDL3/SDL_events.h>
#include <newbase/utility/glm.hpp>
#include <newbase/components/structure.hpp>
#include <newbase/components/spatial.hpp>
#include <newbase/components/camera.hpp>
#include <newbase/components/layers.hpp>
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
static bool _render_layers_enabled = false;
static bool _show_demo = false;
static int _log_observer = -1;
static console c;
static nb::res_browser _res_browser;
static std::vector<nb::res_editor_window> _res_editors;
static nb::about_window _about;
static nb::rtti_window _rtti;
static nb::hash_window _hash;
static nb::render_layers_window _render_layers;

static entt::entity _selected_entity  = entt::null;

// editor camera state
static entt::entity _editor_cam_eid   = entt::null;
static bool  _override_layers         = false;
static float _editor_cam_x            = 0.f;
static float _editor_cam_y            = 0.f;
static float _editor_cam_zoom         = 1.f;
static bool  _panning                 = false;

static void _sync_editor_cam_to_game()
{
    const auto &layers = engine::instance().render_layers();
    if (layers.empty()) return;
    const auto &first = layers.front();
    auto *sc = engine::instance().find_scene(first.scene_id);
    if (!sc) return;
    auto &reg = sc->registry();
    if (first.camera != entt::null)
    {
        if (auto *sp  = reg.try_get<cspatial>(first.camera)) { _editor_cam_x = sp->pos.x; _editor_cam_y = sp->pos.y; }
        if (auto *cam = reg.try_get<ccamera> (first.camera)) { _editor_cam_zoom = cam->zoom; }
    }
}

static void _apply_override_layers()
{
    auto *rs = entt::locator<renderer_service*>::value();
    if (!rs || _editor_cam_eid == entt::null) return;
    ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_DockingEmptyBg);
    render_layer rl;
    rl.scene_id   = entt::null;
    rl.layer_mask = clayers::MASK_ALL;
    rl.camera     = _editor_cam_eid;
    rl.viewport   = rs->default_viewport();
    rl.order      = 0;
    rl.clear_bg   = false;
    rl.use_grid   = true;
    rl.clear_r    = bg.x;
    rl.clear_g    = bg.y;
    rl.clear_b    = bg.z;
    engine::instance().set_override_render_layers({rl});
}

static void _ensure_editor_cam()
{
    auto &reg = engine::instance().default_scene().registry();
    if (_editor_cam_eid != entt::null && reg.valid(_editor_cam_eid)) return;
    _editor_cam_eid = reg.create();
    reg.emplace<cspatial>(_editor_cam_eid);
    reg.emplace<ccamera> (_editor_cam_eid);
    if (_override_layers && _enabled)
        _apply_override_layers();
}

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
        if (_enabled)
        {
            _override_layers = true;
            _ensure_editor_cam();
            _sync_editor_cam_to_game();
            _apply_override_layers();
        }
        else
        {
            _panning = false;
            _override_layers = false;
            engine::instance().clear_override_render_layers();
        }
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

    if(phase == step_phase::POST_UPDATE && _enabled && _override_layers)
    {
        // Maintain editor camera entity across scene changes
        _ensure_editor_cam();
        auto &reg = engine::instance().default_scene().registry();
        if (_editor_cam_eid != entt::null && reg.valid(_editor_cam_eid))
        {
            if (auto *sp = reg.try_get<cspatial>(_editor_cam_eid))
            {
                sp->pos.x = _editor_cam_x;
                sp->pos.y = _editor_cam_y;
            }
            if (auto *cam = reg.try_get<ccamera>(_editor_cam_eid))
                cam->zoom = _editor_cam_zoom;
        }
    }

    if(phase == step_phase::POST_UPDATE && _enabled)
    {
        float fnt_size_unit = ImGui::GetFontSize();
        auto &scn = engine::instance().default_scene();
        auto &reg = scn.registry();

        _draw_main_menu();

        ImGui::Begin(ICON_FK_TABLE " Entities");

            // Draw component icon strip for an entity into the current column
            auto draw_icons = [&](entt::entity id) {
                for (auto&& curr : reg.storage()) {
                    if (auto& storage = curr.second; storage.contains(id)) {
                        auto comp_type = entt::resolve(curr.first);
                        if (comp_type.info() == entt::type_id<void>()) continue;
                        rtti::type_info *info = comp_type.custom();
                        if (!info || info->type_class != rtti::TYPE_CLASS_COMPONENT) continue;
                        ImGui::SameLine();
                        ImGui::TextUnformatted(info->data.component.editor_icon ? info->data.component.editor_icon : "?");
                    }
                }
            };

            // Emit a selectable row; returns true if this entity should expand its tree node
            auto draw_row = [&](entt::entity id, bool has_children, int depth) -> bool {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(entt::to_integral(id)));

                // Build label: name if available, else hex id
                char label[64];
                auto *s = reg.try_get<cstructure>(id);
                if (s && s->has_name())
                    std::snprintf(label, sizeof(label), "%s", s->name);
                else
                    std::snprintf(label, sizeof(label), "%x", entt::to_integral(id));

                ImGuiTreeNodeFlags node_flags =
                    ImGuiTreeNodeFlags_OpenOnArrow |
                    ImGuiTreeNodeFlags_SpanFullWidth |
                    (_selected_entity == id ? ImGuiTreeNodeFlags_Selected : 0);
                if (!has_children)
                    node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

                bool open = ImGui::TreeNodeEx(label, node_flags);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                    _selected_entity = id;

                ImGui::TableNextColumn();
                draw_icons(id);
                ImGui::PopID();
                return open && has_children;
            };

            // Recursive hierarchy draw
            std::function<void(entt::entity)> draw_tree = [&](entt::entity id) {
                auto *s = reg.try_get<cstructure>(id);
                bool has_children = s && s->first_child != entt::null;
                bool open = draw_row(id, has_children, 0);
                if (open) {
                    entt::entity child = s->first_child;
                    while (child != entt::null && reg.valid(child)) {
                        draw_tree(child);
                        child = reg.get<cstructure>(child).next_sibling;
                    }
                    ImGui::TreePop();
                }
            };

            if (ImGui::BeginTable("##entitiestable", 2,
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg,
                {-FLT_MIN, -FLT_MIN}))
            {
                ImGui::TableSetupColumn("entity", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("components", ImGuiTableColumnFlags_WidthFixed, fnt_size_unit * 8.f);
                ImGui::TableHeadersRow();

                // --- Structured entities: hierarchy roots first ---
                for (auto [id, s] : reg.view<cstructure>().each()) {
                    if (s.parent == entt::null)
                        draw_tree(id);
                }

                // --- Separator + unstructured entities ---
                bool any_unstructured = false;
                for (auto id : reg.view<entt::entity>(entt::exclude<cstructure>))
                    { any_unstructured = true; break; }

                if (any_unstructured) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Separator();
                    ImGui::TableNextColumn();
                    ImGui::Separator();
                    for (auto id : reg.view<entt::entity>(entt::exclude<cstructure>))
                        draw_row(id, false, 0);
                }

                ImGui::EndTable();
            }
        ImGui::End();

        ImGui::Begin(ICON_FK_PENCIL " Properties");
        if (_selected_entity != entt::null && reg.valid(_selected_entity))
        {
            auto *_sel_s = reg.try_get<cstructure>(_selected_entity);
            if (_sel_s && _sel_s->has_name())
                ImGui::TextDisabled("%s  [%x]", _sel_s->name, entt::to_integral(_selected_entity));
            else
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

        if(_render_layers_enabled)
            _render_layers.draw(&_render_layers_enabled);

    }
    return true;
}

bool editor::event(SDL_Event* evt)
{
    if (!_enabled || !_override_layers) return true;
    auto &io = ImGui::GetIO();
    if (io.WantCaptureMouse) return true;

    if (evt->type == SDL_EVENT_MOUSE_BUTTON_DOWN && evt->button.button == SDL_BUTTON_MIDDLE)
        _panning = true;
    if (evt->type == SDL_EVENT_MOUSE_BUTTON_UP && evt->button.button == SDL_BUTTON_MIDDLE)
        _panning = false;

    if (evt->type == SDL_EVENT_MOUSE_MOTION && _panning)
    {
        _editor_cam_x -= evt->motion.xrel / _editor_cam_zoom;
        _editor_cam_y -= evt->motion.yrel / _editor_cam_zoom;
    }

    if (evt->type == SDL_EVENT_MOUSE_WHEEL)
    {
        float factor = evt->wheel.y > 0 ? 1.1f : (1.f / 1.1f);
        _editor_cam_zoom = std::clamp(_editor_cam_zoom * factor, 0.05f, 32.f);
    }

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
            if(ImGui::MenuItem("Console", nullptr, _console_enabled))
            {
                _console_enabled = !_console_enabled;
            }
            if(ImGui::MenuItem("Editor", nullptr, _enabled))
            {
                _enabled = !_enabled;
            }

            if(ImGui::MenuItem("Render Layers", nullptr, _render_layers_enabled))
            {
                _render_layers_enabled = true;
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
                if(ImGui::MenuItem("Enemymouse"))
                    imgui_style_extra_enemymouse();
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

        // Editor cam toggle button
        {
            ImVec4 active_col = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
            if (_override_layers)
                ImGui::PushStyleColor(ImGuiCol_Button, active_col);
            else
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Button));
            if (ImGui::Button(ICON_FK_VIDEO_CAMERA))
            {
                _override_layers = !_override_layers;
                if (_override_layers)
                {
                    _ensure_editor_cam();
                    _sync_editor_cam_to_game();
                    _apply_override_layers();
                }
                else
                {
                    _panning = false;
                    engine::instance().clear_override_render_layers();
                }
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Editor Camera");
        }

        ImGui::SameLine();

        // Play/Pause button
        if (engine::instance().is_paused())
        {
            if (ImGui::Button(ICON_FK_PLAY))
                engine::instance().set_paused(false);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play");
        }
        else
        {
            if (ImGui::Button(ICON_FK_PAUSE))
                engine::instance().set_paused(true);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pause");
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
