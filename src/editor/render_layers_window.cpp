#include <newbase/editor/render_layers_window.hpp>
#include <newbase/engine.hpp>
#include <newbase/layer.hpp>
#include <newbase/services/renderer_service.hpp>
#include <imgui.h>
#include "IconsForkAwesome.h"
#include <entt/entt.hpp>

namespace nb {

void render_layers_window::draw(bool* p_open)
{
    if (!ImGui::Begin(ICON_FK_PAINT_BRUSH " Render Layers", p_open))
    {
        ImGui::End();
        return;
    }

    const auto &layers = engine::instance().render_layers();

    if (layers.empty())
    {
        ImGui::TextDisabled("(no render layers configured)");
        ImGui::End();
        return;
    }

    static const ImGuiTableFlags table_flags =
        ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;

    if (ImGui::BeginTable("##render_layers", 5, table_flags))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Order",      ImGuiTableColumnFlags_WidthFixed,   50.f);
        ImGui::TableSetupColumn("Scene",      ImGuiTableColumnFlags_WidthFixed,   80.f);
        ImGui::TableSetupColumn("Camera",     ImGuiTableColumnFlags_WidthFixed,   80.f);
        ImGui::TableSetupColumn("Viewport",   ImGuiTableColumnFlags_WidthFixed,   70.f);
        ImGui::TableSetupColumn("Layer Mask", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (const auto &layer : layers)
        {
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::Text("%d", layer.order);

            ImGui::TableNextColumn();
            if (layer.scene_id == 0)
                ImGui::TextDisabled("default");
            else
                ImGui::Text("%08x", layer.scene_id);

            ImGui::TableNextColumn();
            if (layer.camera == entt::null)
                ImGui::TextDisabled("null");
            else
                ImGui::Text("%x", entt::to_integral(layer.camera));

            ImGui::TableNextColumn();
            if (layer.viewport == VIEWPORT_INVALID)
                ImGui::TextDisabled("none");
            else
                ImGui::Text("%u", layer.viewport);

            ImGui::TableNextColumn();
            // Draw 32 tiny toggle-style buttons, one per bit
            ImGui::PushID(&layer);
            for (int bit = 31; bit >= 0; --bit)
            {
                bool set = (layer.layer_mask >> bit) & 1;
                if (set)
                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                else
                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));

                char label[4];
                snprintf(label, sizeof(label), "%d", bit);
                ImGui::SmallButton(label);
                ImGui::PopStyleColor();
                if (bit > 0) ImGui::SameLine(0.f, 1.f);
            }
            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace nb
