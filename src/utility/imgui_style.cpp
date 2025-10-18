#include <newbase/utility/imgui_style.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/log.hpp>
#include "imgui.h"
#include "IconsForkAwesome.h"
#include <entt/entt.hpp>
#include <string>
#include <vector>

using namespace nb;

void nb::imgui_style_setup()
{
    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Text]                   = ImVec4(0.87f, 0.91f, 0.69f, 1.00f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.87f, 0.91f, 0.69f, 0.37f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.00f, 0.13f, 0.17f, 0.94f);
    colors[ImGuiCol_ChildBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.00f, 0.08f, 0.10f, 0.94f);
    colors[ImGuiCol_Border]                 = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.03f, 0.04f, 1.00f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.87f, 0.91f, 0.69f, 0.04f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.87f, 0.91f, 0.69f, 0.14f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.87f, 0.91f, 0.69f, 0.28f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.00f, 0.10f, 0.13f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.00f, 0.13f, 0.17f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.00f, 0.13f, 0.17f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.00f, 0.10f, 0.13f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.00f, 0.53f, 0.67f, 0.26f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.00f, 0.58f, 0.73f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.00f, 0.38f, 0.47f, 1.00f);
    colors[ImGuiCol_CheckMark]              = ImVec4(0.87f, 0.91f, 0.69f, 1.00f);
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.00f, 0.53f, 0.67f, 0.45f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.00f, 0.53f, 0.67f, 0.60f);
    colors[ImGuiCol_Button]                 = ImVec4(0.00f, 0.53f, 0.67f, 0.26f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.00f, 0.58f, 0.73f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.00f, 0.38f, 0.47f, 1.00f);
    colors[ImGuiCol_Header]                 = ImVec4(0.00f, 0.53f, 0.67f, 0.26f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.00f, 0.58f, 0.73f, 1.00f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.00f, 0.38f, 0.47f, 1.00f);
    colors[ImGuiCol_Separator]              = ImVec4(0.87f, 0.91f, 0.69f, 0.36f);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.87f, 0.91f, 0.69f, 0.59f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.87f, 0.91f, 0.69f, 0.78f);
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.87f, 0.91f, 0.69f, 0.36f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.87f, 0.91f, 0.69f, 0.59f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.87f, 0.91f, 0.69f, 0.77f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.00f, 0.42f, 0.53f, 1.00f);
    colors[ImGuiCol_Tab]                    = ImVec4(0.00f, 0.27f, 0.33f, 1.00f);
    colors[ImGuiCol_TabSelected]            = ImVec4(0.00f, 0.53f, 0.67f, 1.00f);
    colors[ImGuiCol_TabSelectedOverline]    = ImVec4(0.00f, 0.27f, 0.33f, 1.00f);
    colors[ImGuiCol_TabDimmed]              = ImVec4(0.00f, 0.18f, 0.22f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected]      = ImVec4(0.14f, 0.26f, 0.42f, 1.00f);
    colors[ImGuiCol_TabDimmedSelectedOverline]  = ImVec4(0.50f, 0.50f, 0.50f, 0.00f);
    colors[ImGuiCol_PlotLines]              = ImVec4(0.79f, 0.91f, 0.25f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]       = ImVec4(0.92f, 0.29f, 0.08f, 1.00f);
    colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.00f, 0.10f, 0.13f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.87f, 0.91f, 0.69f, 0.52f);
    colors[ImGuiCol_TableBorderLight]       = ImVec4(0.87f, 0.91f, 0.69f, 0.19f);
    colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]          = ImVec4(0.00f, 0.10f, 0.13f, 0.38f);
    colors[ImGuiCol_TextLink]               = ImVec4(0.79f, 0.91f, 0.25f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.79f, 0.91f, 0.25f, 0.35f);
    colors[ImGuiCol_DragDropTarget]         = ImVec4(0.79f, 0.91f, 0.25f, 1.00f);
    colors[ImGuiCol_NavCursor]              = ImVec4(0.79f, 0.91f, 0.25f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);

    ImGui::GetStyle().WindowRounding = 8;
    ImGui::GetStyle().FrameRounding = 3;
    ImGui::GetStyle().GrabRounding = 3;
    ImGui::GetStyle().PopupRounding = 3;
    ImGui::GetStyle().ChildRounding = 3;
    ImGui::GetStyle().GrabMinSize = 8;
    ImGui::GetStyle().ScrollbarSize = 14;
}

void nb::imgui_style_fonts_setup(float scale)
{
    std::string mainfont_path = "_nb_core/ttf/iosevka/IosevkaFixed-Regular.ttf";
    std::vector<char> mainfont_data;
    auto mainfont_hash = entt::hashed_string{mainfont_path.c_str()}.value();
    log::info("[imgui_style] system font: %x", mainfont_hash);
    if(rman().read_all_sync(mainfont_hash, mainfont_data, false))
    {
        ImFontConfig config;
        strncpy(config.Name, "regular", sizeof(ImFontConfig::Name));
        config.RasterizerDensity = scale;
        void * copy = malloc(mainfont_data.size()); // need to copy, imgui takes ownership
        memcpy(copy, mainfont_data.data(), mainfont_data.size());
        ImGui::GetIO().Fonts->AddFontFromMemoryTTF(copy, mainfont_data.size(), 14, &config);
    }
    else
    {
        log::error("[imgui_style] cannot read system font: %x", mainfont_hash);
    }

    std::string iconfont_path = "_nb_core/ttf/forkawesome/forkawesome-webfont.ttf";
    std::vector<char> iconfont_data;
    auto iconfont_hash = entt::hashed_string{iconfont_path.c_str()}.value();
    log::info("[imgui_style] icon font: %x", iconfont_hash);
    if(rman().read_all_sync(iconfont_hash, iconfont_data, false))
    {
        ImFontConfig config;
        strncpy(config.Name, "icons", sizeof(ImFontConfig::Name));
        config.RasterizerDensity = scale;
        config.MergeMode = true;
        config.GlyphMinAdvanceX = 14.0f;
        void * copy = malloc(iconfont_data.size()); // need to copy, imgui takes ownership
        memcpy(copy, iconfont_data.data(), iconfont_data.size());
        static const ImWchar icon_ranges[] = { ICON_MIN_FK, ICON_MAX_FK, 0 };
        ImGui::GetIO().Fonts->AddFontFromMemoryTTF(copy, iconfont_data.size(), 14, &config, icon_ranges);
    }
    else
    {
        log::error("[imgui_style] cannot read icon font: %x", iconfont_hash);
    }

    std::string boldfont_path = "_nb_core/ttf/iosevka/IosevkaFixed-Bold.ttf";
    std::vector<char> boldfont_data;
    auto boldfont_hash = entt::hashed_string{boldfont_path.c_str()}.value();
    log::info("[imgui_style] bold font: %x", boldfont_hash);
    if(rman().read_all_sync(boldfont_hash, boldfont_data, false))
    {
        ImFontConfig config;
        strncpy(config.Name, "bold", sizeof(ImFontConfig::Name));
        config.RasterizerDensity = scale;
        void * copy = malloc(boldfont_data.size()); // need to copy, imgui takes ownership
        memcpy(copy, boldfont_data.data(), boldfont_data.size());
        ImGui::GetIO().Fonts->AddFontFromMemoryTTF(copy, boldfont_data.size(), 14, &config);
    }
    else
    {
        log::error("[imgui_style] cannot read system font: %x", boldfont_hash);
    }

    ImGui::GetIO().Fonts->Build();
}