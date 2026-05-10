#include <newbase/ui/markdown.hpp>
#include "imgui.h"
#include "imgui_markdown.h"

namespace nb::ui {

void Markdown(std::string_view text)
{
    static const ImGui::MarkdownConfig config {
        /* linkCallback    */ nullptr,
        /* tooltipCallback */ nullptr,
        /* imageCallback   */ nullptr,
        /* linkIcon        */ "",
        /* headingFormats  */ { { nullptr, true }, { nullptr, true }, { nullptr, false } },
        /* userData        */ nullptr,
        /* formatCallback  */ ImGui::defaultMarkdownFormatCallback,
        /* formatFlags     */ ImGuiMarkdownFormatFlags_GithubStyle,
    };

    ImGui::Markdown(text.data(), text.size(), config);
}

} // namespace nb::ui
