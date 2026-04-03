#include <newbase/editor/hash_window.hpp>
#include <entt/entt.hpp>
#include <imgui.h>
#include <cstdio>
#include "IconsForkAwesome.h"

namespace nb {

void hash_window::draw(bool* p_open)
{
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!ImGui::Begin(ICON_FK_HASHTAG " Hash Calculator", p_open))
    {
        ImGui::End();
        return;
    }

    ImGui::InputText("String", _input_buf, sizeof(_input_buf));
    _input = _input_buf;

    entt::id_type hash = entt::hashed_string{_input.c_str()}.value();
    ImGui::Text("Hash: 0x%08x", hash);

    ImGui::End();
}

} // namespace nb