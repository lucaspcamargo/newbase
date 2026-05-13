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
#include <newbase/services/picker_service.hpp>
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
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include "IconsForkAwesome.h"
#include <algorithm>
#include <string>

using namespace nb;
using entt::operator""_hs;

struct nb::editor_p
{
    bool enabled              = false;
    bool console_enabled      = false;
    bool about_enabled        = false;
    bool rtti_enabled         = false;
    bool hash_enabled         = false;
    bool render_layers_enabled= false;
    bool show_demo            = false;
    int  log_observer         = -1;

    console              con;
    nb::res_browser      res_browser;
    std::vector<nb::res_editor_window> res_editors;
    nb::about_window     about;
    nb::rtti_window      rtti;
    nb::hash_window      hash;
    nb::render_layers_window render_layers;

    entt::entity selected_entity  = entt::null;

    // editor camera
    entt::entity editor_cam_eid   = entt::null;
    bool         override_layers  = false;
    float        cam_x            = 0.f;
    float        cam_y            = 0.f;
    float        cam_zoom         = 1.f;
    bool         panning          = false;

    // Central viewport rect in physical pixels, updated each frame from the ImGui dock node.
    // The editor owns this when override layers are active; the UI manager backs off.
    int vp_x = 0, vp_y = 0, vp_w = 0, vp_h = 0;
};

editor::editor()  : _d(new editor_p) {}
editor::~editor() { delete _d; }

void editor::_sync_editor_cam_to_game()
{
    const auto &layers = engine::instance().render_layers();
    if (layers.empty()) return;
    const auto &first = layers.front();
    auto *sc = engine::instance().find_scene(first.scene_id);
    if (!sc) return;
    auto &reg = sc->registry();
    if (first.camera != entt::null)
    {
        if (auto *sp  = reg.try_get<cspatial>(first.camera)) { _d->cam_x = sp->pos.x; _d->cam_y = sp->pos.y; }
        if (auto *cam = reg.try_get<ccamera> (first.camera)) { _d->cam_zoom = cam->zoom; }
    }
}

void editor::_apply_override_layers()
{
    auto *rs = entt::locator<renderer_service*>::value();
    if (!rs || _d->editor_cam_eid == entt::null) return;
    ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_DockingEmptyBg);
    render_layer rl;
    rl.scene_id   = entt::null;
    rl.layer_mask = clayers::MASK_ALL;
    rl.camera     = _d->editor_cam_eid;
    rl.viewport   = rs->default_viewport();
    rl.order      = 0;
    rl.clear_bg   = false;
    rl.use_grid   = true;
    rl.clear_r    = bg.x;
    rl.clear_g    = bg.y;
    rl.clear_b    = bg.z;
    engine::instance().set_override_render_layers({rl});
}

void editor::_ensure_editor_cam()
{
    auto &reg = engine::instance().default_scene().registry();
    if (_d->editor_cam_eid != entt::null && reg.valid(_d->editor_cam_eid)) return;
    _d->editor_cam_eid = reg.create();
    reg.emplace<cspatial>(_d->editor_cam_eid);
    reg.emplace<ccamera> (_d->editor_cam_eid);
    if (_d->override_layers && _d->enabled)
        _apply_override_layers();
}

bool editor::init(ryml::ConstNodeRef cfg)
{
    log::info("[editor] init");
    _d->log_observer = log::register_observer([this](int category, int prio, const char *msg){
        _d->con.AddLog("[%s] [%s] %s",
            log::priority_str(static_cast<log::priority>(prio)),
            log::category_str(static_cast<log::category>(category)), msg);
    });

    engine::instance().debug_action_register("Console", [this](){
        _d->console_enabled = !_d->console_enabled;
    });

    engine::instance().debug_action_register("Editor", [this](){
        _d->enabled = !_d->enabled;
        engine::instance().set_paused(_d->enabled);
        if (_d->enabled)
        {
            _d->override_layers = true;
            _ensure_editor_cam();
            _sync_editor_cam_to_game();
            _apply_override_layers();
        }
        else
        {
            _d->panning = false;
            _d->override_layers = false;
            engine::instance().clear_override_render_layers();
        }
    });

    auto open_res_editor = [this](entt::id_type type_id, entt::id_type asset_id, std::string_view name) {
        _d->res_editors.emplace_back().open(type_id, asset_id, name);
    };

    ui_manager* ui_mgr = entt::locator<ui_manager*>::value();
    ui_mgr->register_open_resource_editor_callback(open_res_editor);

    return true;
}

bool editor::step(step_phase phase)
{
    if (phase == step_phase::POST_UPDATE && _d->console_enabled)
        _d->con.Draw(ICON_FK_TERMINAL " Console", &_d->console_enabled);

    if (phase == step_phase::POST_UPDATE && _d->enabled && _d->override_layers)
    {
        // Update central viewport rect from the ImGui dock node (we own it while override is active)
        auto *ui_mgr = entt::locator<ui_manager*>::value();
        ImGuiID ds_id = ui_mgr ? static_cast<ImGuiID>(ui_mgr->dockspace_id()) : 0;
        if (ImGuiDockNode *node = ds_id ? ImGui::DockBuilderGetCentralNode(ds_id) : nullptr)
        {
            const ImGuiIO &io = ImGui::GetIO();
            float sx = io.DisplayFramebufferScale.x > 0.f ? io.DisplayFramebufferScale.x : 1.f;
            float sy = io.DisplayFramebufferScale.y > 0.f ? io.DisplayFramebufferScale.y : 1.f;
            _d->vp_x = static_cast<int>(node->Pos.x  * sx);
            _d->vp_y = static_cast<int>(node->Pos.y  * sy);
            _d->vp_w = static_cast<int>(node->Size.x * sx);
            _d->vp_h = static_cast<int>(node->Size.y * sy);
            if (auto *rs = entt::locator<renderer_service*>::value())
            {
                viewport_handle dvp = rs->default_viewport();
                if (dvp != VIEWPORT_INVALID && _d->vp_w > 1 && _d->vp_h > 1)
                    rs->update_viewport(dvp, _d->vp_x, _d->vp_y, _d->vp_w, _d->vp_h);
            }
        }

        // Maintain editor camera entity across scene changes
        _ensure_editor_cam();
        auto &reg = engine::instance().default_scene().registry();
        if (_d->editor_cam_eid != entt::null && reg.valid(_d->editor_cam_eid))
        {
            if (auto *sp = reg.try_get<cspatial>(_d->editor_cam_eid))
                { sp->pos.x = _d->cam_x; sp->pos.y = _d->cam_y; }
            if (auto *cam = reg.try_get<ccamera>(_d->editor_cam_eid))
                cam->zoom = _d->cam_zoom;
        }
    }

    if (phase == step_phase::POST_UPDATE && _d->enabled)
    {
        float fnt_size_unit = ImGui::GetFontSize();
        auto &scn = engine::instance().default_scene();
        auto &reg = scn.registry();

        _draw_main_menu();

        // Gizmo overlay — only when editor camera is active (override layers)
        if (_d->override_layers && _d->selected_entity != entt::null && _d->vp_w > 0 && _d->vp_h > 0)
        {
            const ImGuiIO &io = ImGui::GetIO();
            const float sx = io.DisplayFramebufferScale.x > 0.f ? io.DisplayFramebufferScale.x : 1.f;
            const float sy = io.DisplayFramebufferScale.y > 0.f ? io.DisplayFramebufferScale.y : 1.f;
            // ImGuizmo works in logical pixels
            const float lvp_x = _d->vp_x / sx;
            const float lvp_y = _d->vp_y / sy;
            const float lvp_w = _d->vp_w / sx;
            const float lvp_h = _d->vp_h / sy;

            ImGui::SetNextWindowPos({lvp_x, lvp_y});
            ImGui::SetNextWindowSize({lvp_w, lvp_h});
            ImGui::SetNextWindowBgAlpha(0.f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0,0});
            ImGui::Begin("##gizmo_overlay", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoDocking);
            ImGui::PopStyleVar();

            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(lvp_x, lvp_y, lvp_w, lvp_h);
            ImGuizmo::SetOrthographic(true);

            // Build view matrix from editor camera (inverse of camera transform)
            // View: translate by -cam, no rotation for 2D
            glm::mat4 view = glm::translate(glm::mat4{1.f}, glm::vec3{-_d->cam_x, -_d->cam_y, -1.f});

            // Orthographic projection matching the editor camera zoom.
            // Use physical pixel dimensions to match render_simple's world→screen mapping
            // (render_simple uses the physical viewport for its viewproj, so world units = physical pixels).
            const float half_w = (_d->vp_w * 0.5f) / _d->cam_zoom;
            const float half_h = (_d->vp_h * 0.5f) / _d->cam_zoom;
            glm::mat4 proj = glm::ortho(-half_w, half_w, half_h, -half_h, -1000.f, 1000.f);

            auto *sp = reg.try_get<cspatial>(_d->selected_entity);
            if (sp)
            {
                glm::mat4 world = sp->world;
                if (ImGuizmo::Manipulate(
                        glm::value_ptr(view), glm::value_ptr(proj),
                        ImGuizmo::TRANSLATE, ImGuizmo::LOCAL,
                        glm::value_ptr(world)))
                {
                    // Decompose modified matrix back to pos/rot/scale
                    float t[3], r[3], s[3];
                    ImGuizmo::DecomposeMatrixToComponents(
                        glm::value_ptr(world), t, r, s);
                    sp->pos.x = t[0];
                    sp->pos.y = t[1];
                    sp->pos.z = t[2];
                    sp->rot   = { r[0], r[1], r[2] };
                    sp->scale = { s[0], s[1], s[2] };
                    sp->apply();
                }
            }

            ImGui::End();
        }

        ImGui::Begin(ICON_FK_TABLE " Entities");

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

            auto draw_row = [&](entt::entity id, bool has_children, int /*depth*/) -> bool {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(entt::to_integral(id)));

                char label[64];
                auto *s = reg.try_get<cstructure>(id);
                if (s && s->has_name())
                    std::snprintf(label, sizeof(label), "%s", s->name);
                else
                    std::snprintf(label, sizeof(label), "%x", entt::to_integral(id));

                ImGuiTreeNodeFlags node_flags =
                    ImGuiTreeNodeFlags_OpenOnArrow |
                    ImGuiTreeNodeFlags_SpanFullWidth |
                    (_d->selected_entity == id ? ImGuiTreeNodeFlags_Selected : 0);
                if (!has_children)
                    node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

                bool open = ImGui::TreeNodeEx(label, node_flags);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                    _d->selected_entity = id;

                ImGui::TableNextColumn();
                draw_icons(id);
                ImGui::PopID();
                return open && has_children;
            };

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

                for (auto [id, s] : reg.view<cstructure>().each())
                    if (s.parent == entt::null) draw_tree(id);

                bool any_unstructured = false;
                for (auto id : reg.view<entt::entity>(entt::exclude<cstructure>))
                    { any_unstructured = true; break; }

                if (any_unstructured) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::Separator();
                    ImGui::TableNextColumn(); ImGui::Separator();
                    for (auto id : reg.view<entt::entity>(entt::exclude<cstructure>))
                        draw_row(id, false, 0);
                }

                ImGui::EndTable();
            }
        ImGui::End();

        ImGui::Begin(ICON_FK_PENCIL " Properties");
        if (_d->selected_entity != entt::null && reg.valid(_d->selected_entity))
        {
            auto *sel_s = reg.try_get<cstructure>(_d->selected_entity);
            if (sel_s && sel_s->has_name())
                ImGui::TextDisabled("%s  [%x]", sel_s->name, entt::to_integral(_d->selected_entity));
            else
                ImGui::TextDisabled("entity %x", entt::to_integral(_d->selected_entity));
            ImGui::Separator();
            for (auto&& curr : reg.storage())
            {
                auto& storage = curr.second;
                if (!storage.contains(_d->selected_entity)) continue;
                auto comp_type = entt::resolve(curr.first);
                if (comp_type.info() == entt::type_id<void>()) continue;
                rtti::type_info *info = comp_type.custom();
                if (!info || info->type_class != rtti::TYPE_CLASS_COMPONENT) continue;

                const char *comp_name = info->identifier.operator const char*();
                void *void_val = storage.value(_d->selected_entity);
                auto ref = comp_type.from_void(void_val);

                ImGui::PushID(static_cast<int>(curr.first));
                if (ImGui::CollapsingHeader(comp_name, ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (draw_meta_any_editor(comp_name, ref))
                        if (info->data.component.notify)
                            info->data.component.notify(reg, _d->selected_entity, void_val);
                }
                ImGui::PopID();
            }
        }
        else
        {
            _d->selected_entity = entt::null;
            ImGui::TextDisabled("(select an entity)");
        }
        ImGui::End();

        _d->res_browser.draw(ICON_FK_ARCHIVE " Resources");

        // Index-based: a resource editor may open another one
        for (size_t i = 0, n = _d->res_editors.size(); i < n; )
        {
            bool open = true;
            _d->res_editors[i].draw(&open);
            if (!open) { _d->res_editors.erase(_d->res_editors.begin() + i); --n; }
            else ++i;
        }

        if (_d->show_demo)          ImGui::ShowDemoWindow(&_d->show_demo);
        if (_d->about_enabled)      _d->about.draw(&_d->about_enabled);
        if (_d->rtti_enabled)       _d->rtti.draw(&_d->rtti_enabled);
        if (_d->hash_enabled)       _d->hash.draw(&_d->hash_enabled);
        if (_d->render_layers_enabled) _d->render_layers.draw(&_d->render_layers_enabled);
    }
    return true;
}

bool editor::event(SDL_Event* evt)
{
    if (!_d->enabled || !_d->override_layers) return true;
    auto &io = ImGui::GetIO();
    if (io.WantCaptureMouse) return true;

    if (evt->type == SDL_EVENT_MOUSE_BUTTON_DOWN && evt->button.button == SDL_BUTTON_LEFT
        && !ImGuizmo::IsOver())
    {
        auto *picker = entt::locator<picker_service*>::value();
        auto *rs     = entt::locator<renderer_service*>::value();
        if (picker && rs)
        {
            // evt->button.x/y are logical pixels; vp_x/y are physical — scale to match
            const ImGuiIO &io = ImGui::GetIO();
            const float sx = io.DisplayFramebufferScale.x > 0.f ? io.DisplayFramebufferScale.x : 1.f;
            const float sy = io.DisplayFramebufferScale.y > 0.f ? io.DisplayFramebufferScale.y : 1.f;
            const float vp_x = evt->button.x * sx - _d->vp_x;
            const float vp_y = evt->button.y * sy - _d->vp_y;

            render_layer rl;
            rl.scene_id   = entt::null;
            rl.layer_mask = clayers::MASK_ALL;
            rl.camera     = _d->editor_cam_eid;
            rl.viewport   = rs->default_viewport();

            entt::entity hit = picker->pick(rl, vp_x, vp_y);
            if (hit != _d->editor_cam_eid)
                _d->selected_entity = hit;
        }
    }

    if (evt->type == SDL_EVENT_MOUSE_BUTTON_DOWN && evt->button.button == SDL_BUTTON_MIDDLE
        && !ImGuizmo::IsUsing())
        _d->panning = true;
    if (evt->type == SDL_EVENT_MOUSE_BUTTON_UP && evt->button.button == SDL_BUTTON_MIDDLE)
        _d->panning = false;

    if (evt->type == SDL_EVENT_MOUSE_MOTION && _d->panning && !ImGuizmo::IsUsing())
    {
        _d->cam_x -= evt->motion.xrel / _d->cam_zoom;
        _d->cam_y -= evt->motion.yrel / _d->cam_zoom;
    }

    if (evt->type == SDL_EVENT_MOUSE_WHEEL)
    {
        float factor = evt->wheel.y > 0 ? 1.1f : (1.f / 1.1f);
        _d->cam_zoom = std::clamp(_d->cam_zoom * factor, 0.05f, 32.f);
    }

    return true;
}

void editor::_draw_main_menu()
{
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("Exit", "Esc"))
            engine::instance().request_exit();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        if (ImGui::MenuItem("Console", nullptr, _d->console_enabled))
            _d->console_enabled = !_d->console_enabled;
        if (ImGui::MenuItem("Editor", nullptr, _d->enabled))
            _d->enabled = !_d->enabled;
        if (ImGui::MenuItem("Render Layers", nullptr, _d->render_layers_enabled))
            _d->render_layers_enabled = true;

        ImGui::Separator();

        if (ImGui::BeginMenu("Theme"))
        {
            if (ImGui::MenuItem("Default"))   imgui_style_setup();
            if (ImGui::MenuItem("Dark"))      ImGui::StyleColorsDark();
            if (ImGui::MenuItem("Light"))     ImGui::StyleColorsLight();
            if (ImGui::MenuItem("Classic"))   ImGui::StyleColorsClassic();
            if (ImGui::MenuItem("Enemymouse"))imgui_style_extra_enemymouse();
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help"))
    {
        if (ImGui::MenuItem("ImGui demo", nullptr, _d->show_demo)) _d->show_demo = !_d->show_demo;
        if (ImGui::MenuItem("RTTI Info"))        _d->rtti_enabled = true;
        if (ImGui::MenuItem("Hash Calculator"))  _d->hash_enabled = true;
        if (ImGui::MenuItem("About"))            _d->about_enabled = true;
        ImGui::EndMenu();
    }

    // Separator + editor cam + play/pause buttons
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    {
        ImVec4 active_col = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
        ImGui::PushStyleColor(ImGuiCol_Button,
            _d->override_layers ? active_col : ImGui::GetStyleColorVec4(ImGuiCol_Button));
        if (ImGui::Button(ICON_FK_VIDEO_CAMERA " Camera"))
        {
            _d->override_layers = !_d->override_layers;
            if (_d->override_layers)
            {
                _ensure_editor_cam();
                _sync_editor_cam_to_game();
                _apply_override_layers();
            }
            else
            {
                _d->panning = false;
                engine::instance().clear_override_render_layers();
            }
        }
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();

    if (engine::instance().is_paused())
    {
        if (ImGui::Button(ICON_FK_PLAY " Play"))   engine::instance().set_paused(false);
    }
    else
    {
        if (ImGui::Button(ICON_FK_PAUSE " Pause")) engine::instance().set_paused(true);
    }

    ImGui::EndMainMenuBar();
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
