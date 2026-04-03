#include <newbase/editor/rtti_window.hpp>
#include <newbase/reflection/rtti_dump.hpp>
#include <imgui.h>
#include "IconsForkAwesome.h"

namespace nb {

void rtti_window::_ensure_loaded()
{
    if (_loaded) return;
    _loaded = true;
    _rtti_text = dump_rtti_info();
}

void rtti_window::draw(bool* p_open)
{
    _ensure_loaded();

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!ImGui::Begin(ICON_FK_CODE " RTTI Info", p_open, ImGuiWindowFlags_HorizontalScrollbar))
    {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted(_rtti_text.c_str());

    ImGui::End();
}

} // namespace nb