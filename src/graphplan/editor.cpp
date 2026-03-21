#include <newbase/graphplan/editor.hpp>
#include <newbase/graphplan/plan.hpp>

#include "imgui_node_editor.h"
#include "imgui.h"

using namespace nb::graphplan;
namespace ed = ax::NodeEditor;

struct nb::graphplan::editor_p
{
    plan &pl;
    ed::Config *config {nullptr};
    ed::EditorContext *ctx {nullptr};
    bool first_frame {true};
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


// GUI

void editor::draw()
{
    assert(_d);

    bool reframe = ImGui::Button("Reframe");

    ed::SetCurrentEditor(_d->ctx);
    ed::Begin("graphplan_editor");

    //
    // 1) Commit known data to editor
    //

    // Submit Nodes
    for(auto &node_p : _d->pl.nodes)
    {
        const auto &node = node_p.second;
        if(_d->first_frame)
            ed::SetNodePosition(node.id, ImVec2{node.pos_x, node.pos_y});
        ed::BeginNode(node.id);
        ImGui::Text("Node #%llu, type #%d", node.id, static_cast<int>(node.type));
        for(auto in_pin : node.input_pins)
        {
            ed::BeginPin(in_pin, ed::PinKind::Input);
            ImGui::Text("-> In %llu", in_pin);
            ed::EndPin();
        }
        for(auto out_pin : node.output_pins)
        {
            ed::BeginPin(out_pin, ed::PinKind::Output);
            ImGui::Text("Out %llu ->", out_pin);
            ed::EndPin();
        }
        ed::EndNode();
    }

    // Submit Links
    for (auto& linkInfo : _d->pl.links)
    {
        ed::Link(linkInfo.second.id, linkInfo.second.input_pin, linkInfo.second.output_pin);
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
                // ed::AcceptNewItem() return true when user release mouse button.
                if (ed::AcceptNewItem())
                {
                    // Since we accepted new link, lets add one to our list of links.
                    uint64_t link_id = _d->pl.get_next_unique_id();
                    const auto link_d = link_data{link_id, inputPinId.Get(), outputPinId.Get()};
                    _d->pl.links.emplace(link_id, link_d);

                    // Draw new link.
                    ed::Link(link_d.id, link_d.input_pin, link_d.output_pin);
                }

                // You may choose to reject connection between these nodes
                // by calling ed::RejectNewItem(). This will allow editor to give
                // visual feedback by changing link thickness and color.
            }
        }
        ed::EndCreate(); // Wraps up object creation action handling.
    }


    // Handle deletion action
    if (ed::BeginDelete())
    {
        // There may be many links marked for deletion, let's loop over them.
        ed::LinkId deletedLinkId;
        while (ed::QueryDeletedLink(&deletedLinkId))
        {
            // If you agree that link can be deleted, accept deletion.
            if (ed::AcceptDeletedItem())
            {
                if(auto it = _d->pl.links.find(deletedLinkId.Get()); it != _d->pl.links.end())
                {
                    _d->pl.links.erase(it);
                }
            }

            // You may reject link deletion by calling:
            // ed::RejectDeletedItem();
        }
        ed::EndDelete(); // Wrap up deletion action
    }

    // End of interaction with editor.
    ed::End();

    if (_d->first_frame || reframe)
        ed::NavigateToContent(0.0f);
    ed::SetCurrentEditor(nullptr);

    _d->first_frame = false;
}