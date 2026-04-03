#include <newbase/editor/text_editor_widget.hpp>
#include <imgui.h>

namespace nb {

void text_editor_widget::open(const char* text, size_t len, const char* language)
{
    _text.assign(text, len);
    _language = language;
}

void text_editor_widget::draw()
{
    if (_language)
        ImGui::TextDisabled("%s", _language);

    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::InputTextMultiline("##text", _text.data(), _text.size() + 1,
                              avail, ImGuiInputTextFlags_ReadOnly);
}

} // namespace nb
