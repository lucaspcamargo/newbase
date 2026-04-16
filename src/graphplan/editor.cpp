#include <newbase/graphplan/editor.hpp>
#include <newbase/graphplan/plan.hpp>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_node_editor.h"
#include "imgui.h"
#include "imgui_internal.h"

#include <newbase/ui/layout.hpp>
#include <newbase/ui/meta_any_editor.hpp>
#include <newbase/ui/resource_field_widget.hpp>
#include <newbase/ui/imgui_icons.hpp>

#include <string.h>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

using namespace nb::graphplan;
namespace ed = ax::NodeEditor;

struct nb::graphplan::editor_p
{
    plan &pl;
    ed::Config *config {nullptr};
    ed::EditorContext *ctx {nullptr};
    bool first_frame {true};
    std::unordered_set<uint64_t> positioned_nodes; // nodes already given a position

    // setup node_editor theme colors to match ImGui's current theme
    void apply_theme()    {
        auto& style = ed::GetStyle();
        auto& colors = ed::GetStyle().Colors;
        style.NodeRounding = ImGui::GetStyle().FrameRounding;
        style.GroupRounding = ImGui::GetStyle().FrameRounding;
        colors[ed::StyleColor_Bg] = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
        colors[ed::StyleColor_Grid] = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        colors[ed::StyleColor_NodeBg] = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        colors[ed::StyleColor_NodeBorder] = ImGui::GetStyleColorVec4(ImGuiCol_Border);
        colors[ed::StyleColor_HovNodeBorder] = ImGui::GetStyleColorVec4(ImGuiCol_Border);
        colors[ed::StyleColor_SelNodeBorder] = ImGui::GetStyleColorVec4(ImGuiCol_Border);
        colors[ed::StyleColor_NodeSelRect] = ImGui::GetStyleColorVec4(ImGuiCol_Header);
        colors[ed::StyleColor_NodeSelRectBorder] = ImGui::GetStyleColorVec4(ImGuiCol_Header);
        colors[ed::StyleColor_HovLinkBorder] = ImGui::GetStyleColorVec4(ImGuiCol_Border);
        colors[ed::StyleColor_SelLinkBorder] = ImGui::GetStyleColorVec4(ImGuiCol_Border);
        colors[ed::StyleColor_HighlightLinkBorder] = ImGui::GetStyleColorVec4(ImGuiCol_Border);
        colors[ed::StyleColor_LinkSelRect] = ImGui::GetStyleColorVec4(ImGuiCol_Header);
        colors[ed::StyleColor_LinkSelRectBorder] = ImGui::GetStyleColorVec4(ImGuiCol_Header);
        colors[ed::StyleColor_PinRect] = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
        colors[ed::StyleColor_PinRectBorder] = ImGui::GetStyleColorVec4(ImGuiCol_Border);
        colors[ed::StyleColor_Flow] = ImGui::GetStyleColorVec4(ImGuiCol_Button);
        colors[ed::StyleColor_FlowMarker] = ImGui::GetStyleColorVec4(ImGuiCol_Button);
        colors[ed::StyleColor_GroupBg] = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
        colors[ed::StyleColor_GroupBorder] = ImGui::GetStyleColorVec4(ImGuiCol_Border);

    }

    // Pending source pin when a new-node popup was triggered by dragging from a pin.
    // 0 means no pending pin (popup opened via background right-click).
    uint64_t new_node_from_pin {0};
    ImVec2   open_popup_canvas_pos {}; // canvas-space position where the popup was opened

    // Per-node cached width (from previous frame), used to right-align output pins.
    std::unordered_map<uint64_t, float> node_width_cache;
};

editor::editor(plan &pl)
{
    _d = new editor_p{pl};

    _d->config = new ed::Config();
    _d->ctx = ed::CreateEditor(_d->config);
}

editor::~editor()
{
    ed::DestroyEditor(_d->ctx);
    _d->config = nullptr; // owned by ctx, will be destroyed with it
    delete _d;
    _d = nullptr;
}

// IMGUI UTILS

void ImGuiEx_BeginColumn()
{
    ImGui::BeginGroup();
}

void ImGuiEx_NextColumn()
{
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
}

void ImGuiEx_EndColumn()
{
    ImGui::EndGroup();
}

// PROPERTY DRAG INFERENCE
// Infers DragFloat parameters from the property name.

struct float_drag_params
{
    float       speed  {0.1f};
    float       v_min  {0.f};
    float       v_max  {0.f};   // 0 == unclamped
    const char* fmt    {"%.3f"};
    ImGuiSliderFlags flags {0};
};

static bool str_ends_with(const char* s, const char* suffix)
{
    size_t sl = strlen(s), fl = strlen(suffix);
    return sl >= fl && strcmp(s + sl - fl, suffix) == 0;
}

static float_drag_params infer_float_drag(const char* name)
{
    // dB — gain/threshold/makeup: [-60, 24], 0.5 dB/px
    if (str_ends_with(name, "_db"))
        return {0.5f, -60.f, 24.f, "%.1f dB"};

    // Milliseconds — non-negative, up to 10 s
    if (str_ends_with(name, "_ms"))
        return {1.f, 0.f, 10000.f, "%.0f ms"};

    // Hz — logarithmic, 1..24000
    if (str_ends_with(name, "_hz") || strcmp(name, "frequency") == 0)
        return {0.f, 1.f, 24000.f, "%.0f Hz", ImGuiSliderFlags_Logarithmic};

    // Biquad Q factor
    if (str_ends_with(name, "_q"))
        return {0.01f, 0.1f, 10.f, "%.3f Q"};

    // Normalised [0,1] quantities
    if (strcmp(name, "feedback")  == 0 ||
        strcmp(name, "mix")       == 0 ||
        strcmp(name, "wet")       == 0 ||
        strcmp(name, "amplitude") == 0 ||
        strcmp(name, "room_size") == 0 ||
        strcmp(name, "damping")   == 0)
        return {0.01f, 0.f, 1.f, "%.2f"};

    // Compressor ratio
    if (strcmp(name, "ratio") == 0)
        return {0.1f, 1.f, 20.f, "%.1f:1"};

    // Bit depth
    if (strcmp(name, "bits") == 0)
        return {0.1f, 1.f, 32.f, "%.1f bit"};

    // Sample-rate reduction factor
    if (strcmp(name, "downsample") == 0)
        return {0.1f, 1.f, 32.f, "%.1f x"};

    // Frequency without _hz suffix (e.g. EQ band0_freq)
    if (str_ends_with(name, "_freq"))
        return {0.f, 1.f, 24000.f, "%.0f Hz", ImGuiSliderFlags_Logarithmic};

    // Chorus/phaser depth [0,1]
    if (strcmp(name, "depth") == 0)
        return {0.01f, 0.f, 1.f, "%.2f"};

    // Chorus voices [1,4]
    if (strcmp(name, "voices") == 0)
        return {0.05f, 1.f, 4.f, "%.1f"};

    // Waveshaper drive [1,100]
    if (strcmp(name, "drive") == 0)
        return {0.5f, 1.f, 100.f, "%.1f"};

    // Waveshaper shape mode [0,2]
    if (strcmp(name, "shape") == 0)
        return {0.05f, 0.f, 2.f, "%.2f"};

    // Phaser stages [2,8]
    if (strcmp(name, "stages") == 0)
        return {0.1f, 2.f, 8.f, "%.0f"};

    return {}; // generic fallback
}

// PIN WIDGET
// Draws a pin with a circle connector and a text label.
// Circle is on the left for inputs, right for outputs.
// PinPivotRect is set to the circle so links attach to its center.

static void draw_pin(uint64_t pin_id, ed::PinKind kind, const char* label)
{
    static constexpr float RADIUS  = 5.f;
    static constexpr float SPACING = 5.f;

    const float text_w = ImGui::CalcTextSize(label).x;
    const float text_h = ImGui::GetTextLineHeight();
    const float total_w = RADIUS * 2.f + SPACING + text_w;

    ed::BeginPin(pin_id, kind);

    ImVec2 p  = ImGui::GetCursorScreenPos();
    float  cy = p.y + text_h * 0.5f;

    // Reserve space for the whole widget.
    ImGui::Dummy({total_w, text_h});

    ImDrawList* dl       = ImGui::GetWindowDrawList();
    ImU32       text_col = ImGui::GetColorU32(ImGuiCol_Text);
    ImU32       fill_col = ImGui::GetColorU32(ImGuiCol_FrameBg);
    ImU32       rim_col  = text_col;

    if (kind == ed::PinKind::Input)
    {
        ImVec2 cc = {p.x + RADIUS, cy};
        dl->AddCircleFilled(cc, RADIUS, fill_col);
        dl->AddCircle(cc, RADIUS, rim_col, 12, 1.5f);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                    {p.x + RADIUS * 2.f + SPACING, p.y}, text_col, label);
        ed::PinPivotRect({cc.x, cc.y}, {cc.x, cc.y});
    }
    else
    {
        ImVec2 cc = {p.x + text_w + SPACING + RADIUS, cy};
        dl->AddCircleFilled(cc, RADIUS, fill_col);
        dl->AddCircle(cc, RADIUS, rim_col, 12, 1.5f);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                    {p.x, p.y}, text_col, label);
        ed::PinPivotRect({cc.x, cc.y}, {cc.x, cc.y});
    }

    ed::EndPin();
}


// GUI

bool editor::draw()
{
    assert(_d);

    bool changed = false;
    bool reframe = ImGui::Button(ICON_FK_ARROWS_ALT " Reframe");
    ImGui::SameLine();
    bool reset_zoom = ImGui::Button(ICON_FK_BULLSEYE " Reset Zoom");
    ImGui::SameLine();
    bool show_flow = ImGui::Button(ICON_FK_FORWARD " Show Flow");


    // Purge stale positioned_nodes entries for nodes that no longer exist in the plan.
    for (auto it = _d->positioned_nodes.begin(); it != _d->positioned_nodes.end(); )
    {
        if (_d->pl.nodes.find(*it) == _d->pl.nodes.end())
            it = _d->positioned_nodes.erase(it);
        else
            ++it;
    }

    ed::SetCurrentEditor(_d->ctx);
    ed::Begin("graphplan_editor");

    _d->apply_theme();

    //
    // 1) Commit known data to editor
    //

    // Submit Nodes
    for(auto &node_p : _d->pl.nodes)
    {
        const auto &node = node_p.second;
        if(_d->positioned_nodes.find(node.id) == _d->positioned_nodes.end())
        {
            ed::SetNodePosition(node.id, ImVec2{node.pos_x, node.pos_y});
            _d->positioned_nodes.insert(node.id);
        }

        const auto* type_def = _d->pl.dom().find_type(node.type);

        ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(8, 4, 8, 8));
        ed::BeginNode(node.id);
        ImGui::PushID(node.id);

        // --- Header ---
        // Record top-left before any content; bottom-right after.
        // x_min/x_max will be corrected to full node width in the post-EndNode drawing step.
        ImVec2 header_min = ImGui::GetCursorScreenPos();
        if (type_def)
            ImGui::TextUnformatted(type_def->name);
        else
        {
            char label[32];
            snprintf(label, sizeof(label), "Node #%d", node.type);
            ImGui::TextUnformatted(label);
        }
        float  header_bottom = ImGui::GetItemRectMax().y + ImGui::GetStyle().ItemSpacing.y;
        ImGui::Spacing();

        // --- Custom draw_fn ---
        if (type_def && type_def->draw_fn)
        {
            if (type_def->draw_fn(node_p.second))
                changed = true;
            ImGui::Spacing();
        }

        // --- Input pins ---
        for (auto in_pin : node.input_pins)
        {
            const auto* pi  = _d->pl.pins.count(in_pin) ? &_d->pl.pins.at(in_pin) : nullptr;
            const auto* ptd = pi ? _d->pl.dom().find_pin_type(pi->type_id) : nullptr;
            draw_pin(in_pin, ed::PinKind::Input, ptd ? ptd->name : "?");
        }

        // --- Properties ---
        if (type_def && !type_def->props.empty())
        {
            ImGui::Spacing();
            for (const auto& pdef : type_def->props)
            {
                if (pdef.hide_when_custom_ui && type_def->draw_fn)
                    continue;
                auto& val = node_p.second.properties[pdef.name];
                ImGui::SetNextItemWidth(160.f);
                if (pdef.res_type_id != 0)
                {
                    entt::id_type id = val.try_cast<entt::id_type>()
                        ? *val.try_cast<entt::id_type>() : 0;
                    if (nb::draw_resource_id_field(pdef.name, pdef.res_type_id, id))
                        { val = entt::meta_any{id}; changed = true; }
                }
                else if (auto* v = val.try_cast<float>())
                {
                    float f = *v;
                    const auto p = infer_float_drag(pdef.name);
                    if (ImGui::DragFloat(pdef.name, &f, p.speed, p.v_min, p.v_max, p.fmt, p.flags))
                        { val = entt::meta_any{f}; changed = true; }
                }
                else if (auto* v = val.try_cast<bool>())
                {
                    bool b = *v;
                    if (ImGui::Checkbox(pdef.name, &b))
                        { val = entt::meta_any{b}; changed = true; }
                }
                else if (auto* v = val.try_cast<int>())
                {
                    int i = *v;
                    if (ImGui::DragInt(pdef.name, &i))
                        { val = entt::meta_any{i}; changed = true; }
                }
                else if (auto* v = val.try_cast<std::string>())
                {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "%s", v->c_str());
                    if (ImGui::InputText(pdef.name, buf, sizeof(buf)))
                        { val = entt::meta_any{std::string{buf}}; changed = true; }
                }
                else
                {
                    if (nb::draw_meta_any_editor(pdef.name, val))
                        changed = true;
                }
            }
            ImGui::Spacing();
        }

        // --- Output pins (right-aligned using cached node width) ---
        if (!node.output_pins.empty())
        {
            // Measure output column width.
            static constexpr float PIN_RADIUS  = 5.f;
            static constexpr float PIN_SPACING = 5.f;
            float out_col_w = 0.f;
            for (auto out_pin : node.output_pins)
            {
                const auto* pi  = _d->pl.pins.count(out_pin) ? &_d->pl.pins.at(out_pin) : nullptr;
                const auto* ptd = pi ? _d->pl.dom().find_pin_type(pi->type_id) : nullptr;
                const char* lbl = ptd ? ptd->name : "?";
                out_col_w = std::max(out_col_w,
                    PIN_RADIUS * 2.f + PIN_SPACING + ImGui::CalcTextSize(lbl).x);
            }

            // Use cached node width from the previous frame to compute the shift.
            // cursor_x is the natural left position of the output pins (screen space).
            // node_right = cursor_x + node_width - 2*padding (right edge of content area).
            // shift = node_right - out_col_w - cursor_x = node_width - 2*padding - out_col_w.
            const float pad = 8.f; // NodePadding right
            auto it = _d->node_width_cache.find(node.id);
            if (it != _d->node_width_cache.end())
            {
                float shift = it->second - 2.f * pad - out_col_w;
                if (shift > 0.f)
                    ImGui::SetCursorScreenPos({ImGui::GetCursorScreenPos().x + shift,
                                              ImGui::GetCursorScreenPos().y});
            }

            for (auto out_pin : node.output_pins)
            {
                const auto* pi  = _d->pl.pins.count(out_pin) ? &_d->pl.pins.at(out_pin) : nullptr;
                const auto* ptd = pi ? _d->pl.dom().find_pin_type(pi->type_id) : nullptr;
                draw_pin(out_pin, ed::PinKind::Output, ptd ? ptd->name : "?");
            }
        }

        ImGui::PopID();
        ed::EndNode();
        ed::PopStyleVar(); // NodePadding

        // Cache node width for next frame's output-pin alignment.
        _d->node_width_cache[node.id] = ImGui::GetItemRectSize().x;

        // --- Draw header background ---
        // After EndNode, GetItemRect gives the full node bounds in screen space.
        if (ImGui::IsItemVisible())
        {
            const float alpha       = ImGui::GetStyle().Alpha;
            const float half_border = ed::GetStyle().NodeBorderWidth * 0.5f;
            const float rounding    = ed::GetStyle().NodeRounding;
            const float* hc        = type_def ? type_def->header_color
                                              : node_type_def{}.header_color;
            const ImU32 fill = IM_COL32(
                static_cast<int>(hc[0] * 255),
                static_cast<int>(hc[1] * 255),
                static_cast<int>(hc[2] * 255),
                static_cast<int>(hc[3] * alpha * 255));
            const ImU32 line = IM_COL32(255, 255, 255, static_cast<int>(96 * alpha));

            ImVec2 node_rect_min = ImGui::GetItemRectMin();
            ImVec2 node_rect_max = ImGui::GetItemRectMax();
            ImVec2 r_min = ImVec2(node_rect_min.x + half_border,
                                  node_rect_min.y + half_border);
            ImVec2 r_max = ImVec2(node_rect_max.x - half_border,
                                  header_bottom);

            auto* dl = ed::GetNodeBackgroundDrawList(node.id);
            dl->AddRectFilled(r_min, r_max, fill, rounding, ImDrawFlags_RoundCornersTop);
            dl->AddLine(
                ImVec2(r_min.x, r_max.y - 0.5f),
                ImVec2(r_max.x, r_max.y - 0.5f),
                line, 1.f);
        }
    }

    // Submit Links
    for (auto& linkInfo : _d->pl.links)
    {
        ed::Link(linkInfo.second.id, linkInfo.second.input_pin, linkInfo.second.output_pin,
        ImGui::GetStyleColorVec4(ImGuiCol_Text));
        if(show_flow)
            ed::Flow(linkInfo.second.id, ed::FlowDirection::Backward);
    }

    //
    // 2) Handle interactions
    //

    // Handle creation action, returns true if editor want to create new object (node or link)
    if (ed::BeginCreate())
    {
        ed::PinId inputPinId, outputPinId;
        if (ed::QueryNewLink(&inputPinId, &outputPinId))
        {
            // QueryNewLink returns true if editor want to create new link between pins.
            //
            // Link can be created only for two valid pins, it is up to you to
            // validate if connection make sense. Editor is happy to make any.
            //
            // Link always goes from input to output. User may choose to drag
            // link from output pin or input pin. This determine which pin ids
            // are valid and which are not:
            //   * input valid, output invalid - user started to drag new ling from input pin
            //   * input invalid, output valid - user started to drag new ling from output pin
            //   * input valid, output valid   - user dragged link over other pin, can be validated

            if (inputPinId && outputPinId) // both are valid, let's accept link
            {
                // Determine true direction from plan data — editor pin names are drag-order,
                // not actual kind. A pin is an output if it appears in any node's output_pins.
                auto is_output_pin = [&](uint64_t pin_id) {
                    auto pit = _d->pl.pins.find(pin_id);
                    if (pit == _d->pl.pins.end()) return false;
                    auto nit = _d->pl.nodes.find(pit->second.node_id);
                    if (nit == _d->pl.nodes.end()) return false;
                    const auto& ops = nit->second.output_pins;
                    return std::find(ops.begin(), ops.end(), pin_id) != ops.end();
                };

                // Normalise: src must be output, dst must be input.
                uint64_t src_pin = outputPinId.Get();
                uint64_t dst_pin = inputPinId.Get();
                if (!is_output_pin(src_pin) && is_output_pin(dst_pin))
                    std::swap(src_pin, dst_pin);

                bool dir_ok = is_output_pin(src_pin) && !is_output_pin(dst_pin);

                auto src_it = _d->pl.pins.find(src_pin);
                auto dst_it = _d->pl.pins.find(dst_pin);
                bool compatible = dir_ok
                               && src_it != _d->pl.pins.end()
                               && dst_it != _d->pl.pins.end()
                               && _d->pl.dom().can_connect(src_it->second.type_id,
                                                           dst_it->second.type_id);

                // check if connection between these pins already exists
                for (const auto& lnk : _d->pl.links)
                {
                    if ((lnk.second.input_pin == dst_pin && lnk.second.output_pin == src_pin) ||
                        (lnk.second.input_pin == src_pin && lnk.second.output_pin == dst_pin))
                    {
                        compatible = false;
                        break;
                    }
                }

                if (compatible)
                {
                    // ed::AcceptNewItem() return true when user release mouse button.
                    if (ed::AcceptNewItem(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), 2.0f))
                    {
                        uint64_t link_id = _d->pl.get_next_unique_id();
                        const auto link_d = link_data{link_id, dst_pin, src_pin};
                        _d->pl.links.emplace(link_id, link_d);
                        ed::Link(link_d.id, link_d.input_pin, link_d.output_pin);
                        changed = true;
                    }
                }
                else
                {
                    ed::RejectNewItem(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), 2.0f);
                }
            }
        }
        // Handle dragging from a pin into empty space → open node creation menu.
        ed::PinId newNodeFromPin;
        if (ed::QueryNewNode(&newNodeFromPin))
        {
            if (ed::AcceptNewItem())
            {
                _d->new_node_from_pin     = newNodeFromPin.Get();
                _d->open_popup_canvas_pos = ed::ScreenToCanvas(ImGui::GetMousePos());
                ed::Suspend();
                ImGui::OpenPopup("##create_node");
                ed::Resume();
            }
        }

        ed::EndCreate(); // Wraps up object creation action handling.
    }


    // Deletes a node and all its pins and connected links from the plan.
    auto delete_node = [&](uint64_t node_id) {
        auto node_it = _d->pl.nodes.find(node_id);
        if (node_it == _d->pl.nodes.end()) return;
        const auto& nd = node_it->second;

        // Collect all pin ids belonging to this node.
        std::vector<uint64_t> pin_ids;
        pin_ids.insert(pin_ids.end(), nd.input_pins.begin(),  nd.input_pins.end());
        pin_ids.insert(pin_ids.end(), nd.output_pins.begin(), nd.output_pins.end());

        // Remove all links that reference any of those pins.
        for (auto it = _d->pl.links.begin(); it != _d->pl.links.end(); )
        {
            const auto& lnk = it->second;
            bool connected = false;
            for (uint64_t pid : pin_ids)
                if (lnk.input_pin == pid || lnk.output_pin == pid) { connected = true; break; }
            it = connected ? _d->pl.links.erase(it) : std::next(it);
        }

        // Remove pins then node.
        for (uint64_t pid : pin_ids)
            _d->pl.pins.erase(pid);
        _d->pl.nodes.erase(node_it);
        changed = true;
    };

    // Handle deletion action
    if (ed::BeginDelete())
    {
        // Node deletion (keyboard Delete / Backspace on selected nodes).
        ed::NodeId deletedNodeId;
        while (ed::QueryDeletedNode(&deletedNodeId))
        {
            if (ed::AcceptDeletedItem())
                delete_node(deletedNodeId.Get());
        }

        // Link deletion.
        ed::LinkId deletedLinkId;
        while (ed::QueryDeletedLink(&deletedLinkId))
        {
            if (ed::AcceptDeletedItem())
            {
                if (auto it = _d->pl.links.find(deletedLinkId.Get()); it != _d->pl.links.end())
                {
                    _d->pl.links.erase(it);
                    changed = true;
                }
            }
        }
        ed::EndDelete();
    }

    // Context menu triggers — must be inside Begin/End, suspended from the canvas.
    ed::Suspend();
    ed::NodeId ctx_node_id;
    if (ed::ShowNodeContextMenu(&ctx_node_id))
        ImGui::OpenPopup("##node_ctx");
    if (ed::ShowBackgroundContextMenu())
    {
        _d->new_node_from_pin    = 0;
        _d->open_popup_canvas_pos = ed::ScreenToCanvas(ImGui::GetMousePos());
        ImGui::OpenPopup("##create_node");
    }
    ed::Resume();

    // Node context menu popup.
    ed::Suspend();
    if (ImGui::BeginPopup("##node_ctx"))
    {
        if (ImGui::MenuItem("Delete node"))
            delete_node(ctx_node_id.Get());
        ImGui::EndPopup();
    }
    ed::Resume();

    // Popup rendering — also inside Begin/End, suspended from the canvas.
    ed::Suspend();
    if (ImGui::BeginPopup("##create_node"))
    {
        ImGui::TextDisabled("Add node");
        ImGui::Separator();

        const uint64_t from_pin     = _d->new_node_from_pin;
        const ImVec2   canvas_pos   = _d->open_popup_canvas_pos;
        const int from_pin_type = (from_pin && _d->pl.pins.count(from_pin))
                                  ? _d->pl.pins.at(from_pin).type_id : -1;

        // Collect addable types, filtering by pin compatibility.
        std::vector<const node_type_def*> addable;
        for (const auto& ntd : _d->pl.dom().node_types)
        {
            if (!ntd.user_addable) continue;
            if (from_pin_type >= 0)
            {
                bool any = false;
                for (int t : ntd.input_types)
                    if (_d->pl.dom().can_connect(from_pin_type, t)) { any = true; break; }
                for (int t : ntd.output_types)
                    if (_d->pl.dom().can_connect(t, from_pin_type)) { any = true; break; }
                if (!any) continue;
            }
            addable.push_back(&ntd);
        }

        // Sort categories alphabetically, uncategorised last.
        auto cat_name = [&](int cat_id) -> const char* {
            for (const auto& c : _d->pl.dom().categories)
                if (c.id == cat_id) return c.name;
            return nullptr;
        };
        std::vector<int> cat_order;
        for (const auto& ntd : addable)
            if (std::find(cat_order.begin(), cat_order.end(), ntd->category_id) == cat_order.end())
                cat_order.push_back(ntd->category_id);
        std::sort(cat_order.begin(), cat_order.end(), [&](int a, int b) {
            const char* na = cat_name(a);
            const char* nb = cat_name(b);
            if (!na && !nb) return false;
            if (!na) return false; // uncategorised last
            if (!nb) return true;
            return strcmp(na, nb) < 0;
        });

        // Draw each category as a collapsing header with items sorted alphabetically.
        for (int cat_id : cat_order)
        {
            const char* cname = cat_name(cat_id);
            bool open = true;
            if (cname)
            {
                open = ImGui::CollapsingHeader(cname, ImGuiTreeNodeFlags_DefaultOpen);
                if (open) ImGui::Indent();
            }
            if (!open) continue;

            std::vector<const node_type_def*> in_cat;
            for (auto* ntd : addable)
                if (ntd->category_id == cat_id) in_cat.push_back(ntd);
            std::sort(in_cat.begin(), in_cat.end(), [](const node_type_def* a, const node_type_def* b) {
                return strcmp(a->name, b->name) < 0;
            });

            for (auto* ntd : in_cat)
            if (ImGui::MenuItem(ntd->name))
            {
                uint64_t new_node_id = _d->pl.add_node_from_type(ntd->type_id,
                                                                   canvas_pos.x, canvas_pos.y);
                ed::SetNodePosition(new_node_id, canvas_pos);
                changed = true;

                // If triggered from a pin, auto-connect the compatible side.
                if (from_pin && new_node_id)
                {
                    const auto& new_nd = _d->pl.nodes.at(new_node_id);
                    uint64_t link_src = 0, link_dst = 0;

                    auto& from_pd = _d->pl.pins.at(from_pin);
                    bool from_is_output = false;
                    {
                        auto& owner = _d->pl.nodes.at(from_pd.node_id);
                        for (auto pid : owner.output_pins)
                            if (pid == from_pin) { from_is_output = true; break; }
                    }

                    if (from_is_output)
                    {
                        for (uint64_t in_pin : new_nd.input_pins)
                        {
                            if (_d->pl.dom().can_connect(from_pd.type_id,
                                                          _d->pl.pins.at(in_pin).type_id))
                            {
                                link_src = from_pin;
                                link_dst = in_pin;
                                break;
                            }
                        }
                    }
                    else
                    {
                        for (uint64_t out_pin : new_nd.output_pins)
                        {
                            if (_d->pl.dom().can_connect(_d->pl.pins.at(out_pin).type_id,
                                                          from_pd.type_id))
                            {
                                link_src = out_pin;
                                link_dst = from_pin;
                                break;
                            }
                        }
                    }

                    if (link_src && link_dst)
                    {
                        uint64_t link_id = _d->pl.get_next_unique_id();
                        _d->pl.links.emplace(link_id, link_data{link_id, link_dst, link_src});
                        changed = true;
                    }
                }
            }

            if (cname) ImGui::Unindent();
        } // end category loop
        ImGui::EndPopup();
    }
    ed::Resume();

    // End of interaction with editor.
    ed::End();

    if (_d->first_frame || reframe)
        ed::NavigateToContent(0.0f);
    ed::SetCurrentEditor(nullptr);

    _d->first_frame = false;
    return changed;
}