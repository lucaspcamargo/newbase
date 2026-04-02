#include <newbase/editor/about_window.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/log.hpp>
#include <newbase/nb_config.h>
#include <imgui.h>
#include "IconsForkAwesome.h"
#include <vector>

using namespace nb;
using entt::operator""_hs;

void about_window::_ensure_loaded()
{
    if (_loaded) return;
    _loaded = true;

    std::vector<char> data;
    if (rman().read_all_sync("sbom.spdx"_hs, data, true))
        _sbom.load(data.data());
    else
        log::warn("[about] sbom.spdx not found in resource manager");
}

void about_window::draw(bool* p_open)
{
    _ensure_loaded();

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!ImGui::Begin(ICON_FK_INFO_CIRCLE " About", p_open))
    {
        ImGui::End();
        return;
    }

    ImGui::Text("newbase Engine");
    ImGui::Separator();
    ImGui::Text("Version %s", NEWBASE_VERSION);
    ImGui::Text("Authors: %s", NEWBASE_AUTHORS);
    ImGui::Text("%s", NEWBASE_COPYRIGHT);
    ImGui::Separator();
    ImGui::Text("A little game/multimedia engine of mine.");
    ImGui::TextLinkOpenURL("More info...", NEWBASE_URL);
    ImGui::Separator();

    if (ImGui::TreeNode("Software Bill of Materials (sbom.spdx)"))
    {
        if (!_sbom.empty())
            _sbom.draw();
        else
            ImGui::TextDisabled("sbom.spdx not available");

        ImGui::TreePop();
    }

    ImGui::End();
}
