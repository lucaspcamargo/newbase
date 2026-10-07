#include <newbase/editor/text_editor_widget.hpp>
#include <newbase/ui/imgui_icons.hpp>
#include <imgui.h>
#include <cstdint>  // required by TextEditor
#include <TextEditor.h>

namespace nb {

text_editor_widget::~text_editor_widget()
{
    // nothing so far
}

void text_editor_widget::open(const char* text, size_t len, const char* language)
{
    _editor = std::make_shared<TextEditor>();
    _editor->SetCompletePairedGlyphs(true);
    _language = language;
    if(!strcmp(_language, "Lua"))
    {;
        _editor->SetLanguage(TextEditor::Language::Lua());
    }
    _text.assign(text, len);
    _word_wrap = _editor->IsWordWrapEnabled();
    setPaletteFromTheme();
    _editor->SetText(_text);
    _editor->SetReadOnlyEnabled(false);
}

void text_editor_widget::setPaletteFromTheme()
{
    auto col = ImGui::ColorConvertU32ToFloat4(ImGui::GetColorU32(ImGuiCol_WindowBg));
    if(col.x+col.y+col.z > 1.6f)
        _editor->SetPalette(TextEditor::GetLightPalette());
    else
        _editor->SetPalette(TextEditor::GetDarkPalette());
}

void text_editor_widget::draw()
{
    auto pos = _editor->GetCurrentCursorPosition();
    ImGui::Text("%s -- line %zu, col %zu   ",
                _language? _language : "Text",
                pos.line + 1, pos.index + 1);

    // options menu
    ImGui::SameLine();
    float posX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX(posX);
    if (ImGui::Button(ICON_FK_BARS)) {
        ImGui::OpenPopup("text_editor_options_popup");
    }
    static const char* themes[] = { "Auto", "Dark", "Light", "Editor Default" };

    // 2. Renderize o menu/popup (esta função só retorna true se o popup estiver aberto)
    if (ImGui::BeginPopup("text_editor_options_popup")) {
        ImGui::Checkbox("Show whitespace", &_draw_whitespace);
        ImGui::Checkbox("Word Wrap", &_word_wrap);
        ImGui::Separator();
        for(int i = 0; i < 4; i++)
            if(ImGui::MenuItem(themes[i], nullptr, _theme_idx == i))
                _theme_idx = i;
        ImGui::EndPopup();
    }

    // apply options

    _editor->SetShowWhitespacesEnabled(_draw_whitespace);
    _editor->SetWordWrapEnabled(_word_wrap);
    switch (_theme_idx) {
        case 1:
            _editor->SetPalette(TextEditor::GetDarkPalette());
            break;
        case 2:
            _editor->SetPalette(TextEditor::GetLightPalette());
            break;
        case 3:
            _editor->SetPalette(TextEditor::GetDefaultPalette());
            break;
        default:
            setPaletteFromTheme();
            break;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    _editor->Render("TITLE???", avail, true);
}

} // namespace nb
