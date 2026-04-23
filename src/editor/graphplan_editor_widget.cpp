#include <newbase/editor/graphplan_editor_widget.hpp>
#include <newbase/graphplan/plan.hpp>
#include <newbase/graphplan/editor.hpp>
#include <newbase/graphplan/domain_registry.hpp>
#include <newbase/res/graphplan.hpp>
#include <newbase/res/writers.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/log.hpp>
#include "IconsForkAwesome.h"
#include <imgui.h>
#include <unordered_map>

namespace nb {

graphplan_editor_widget::graphplan_editor_widget() = default;
graphplan_editor_widget::~graphplan_editor_widget() = default;

bool graphplan_editor_widget::open(const rgraphplan* res, entt::id_type asset_id)
{
    _plan.reset();
    _editor.reset();
    _path.clear();

    const graphplan::domain* dom = graphplan::find_domain(res->domain_id.c_str());
    if (!dom)
    {
        log::warn("[graphplan_editor_widget] domain '%s' not registered", res->domain_id.c_str());
        return false;
    }

    _plan = std::make_unique<graphplan::plan>(*dom);

    // Rebuild live plan from the serialised resource — same logic as _apply_rgraphplan
    // but without audio-manager cache concerns.
    std::unordered_map<uint64_t, uint64_t> id_remap;

    for (const auto& nd : res->nodes)
    {
        const auto* tdef = dom->find_type_by_name(nd.type_name.c_str());
        if (!tdef)
        {
            log::warn("[graphplan_editor_widget] unknown type '%s' — skipped", nd.type_name.c_str());
            continue;
        }
        uint64_t new_id = _plan->add_node_from_type(tdef->type_id, nd.pos_x, nd.pos_y);
        id_remap[nd.id] = new_id;

        auto& new_nd = _plan->nodes.at(new_id);
        for (const auto& [pname, pval] : nd.properties)
            new_nd.properties[pname] = pval;
    }

    for (const auto& ld : res->links)
    {
        auto from_it = id_remap.find(ld.from_node);
        auto to_it   = id_remap.find(ld.to_node);
        if (from_it == id_remap.end() || to_it == id_remap.end()) continue;

        auto& from_nd = _plan->nodes.at(from_it->second);
        auto& to_nd   = _plan->nodes.at(to_it->second);
        if (ld.from_pin < 0 || ld.from_pin >= (int)from_nd.output_pins.size()) continue;
        if (ld.to_pin   < 0 || ld.to_pin   >= (int)to_nd.input_pins.size())   continue;

        uint64_t lid     = _plan->get_next_unique_id();
        uint64_t out_pin = from_nd.output_pins[ld.from_pin];
        uint64_t in_pin  = to_nd.input_pins[ld.to_pin];
        _plan->links.insert({lid, graphplan::link_data{lid, in_pin, out_pin}});
    }

    _editor = std::make_unique<graphplan::editor>(*_plan);

    auto it = rman().handles().find(asset_id);
    if (it != rman().handles().end())
        _path = it->second.path;

    return true;
}

void graphplan_editor_widget::draw()
{
    ImGui::SetNextItemWidth(320.f);
    ImGui::InputText("##path", _path.data(), _path.size() + 1,
                     ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FK_FLOPPY_O " Save") && _plan && !_path.empty())
        write_graphplan_plan(*_plan, _path.c_str());
    ImGui::Separator();

    if (_editor)
        _editor->draw();
    else
        ImGui::TextDisabled("(domain not registered)");
}

} // namespace nb
