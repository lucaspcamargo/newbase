#include <newbase/editor/spdx_table.hpp>
#include <imgui.h>
#include <string_view>

using namespace nb;

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

static std::string_view sv_trim(std::string_view s)
{
    const char* ws = " \t\r";
    auto start = s.find_first_not_of(ws);
    if (start == std::string_view::npos) return {};
    auto end = s.find_last_not_of(ws);
    return s.substr(start, end - start + 1);
}

static bool sv_starts_with(std::string_view s, std::string_view prefix)
{
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

// ---------------------------------------------------------------------------
// parsing
// ---------------------------------------------------------------------------

void spdx_table::load(std::string_view raw_text)
{
    _raw = std::string(raw_text);
    _parse(raw_text);
}

void spdx_table::_parse(std::string_view text)
{
    _packages.clear();

    package current;
    bool in_pkg       = false;
    bool in_multiline = false;
    std::string ml_key;
    std::string ml_accum;

    // Commit current package and reset
    auto flush = [&]() {
        if (in_pkg && !current.name.empty())
            _packages.push_back(std::move(current));
        current    = {};
        in_pkg     = false;
    };

    // Strip <text>…</text> wrapper if present
    auto strip_text_tags = [](std::string_view v) -> std::string {
        const std::string_view open  = "<text>";
        const std::string_view close = "</text>";
        if (sv_starts_with(v, open)) {
            v = v.substr(open.size());
            auto end = v.rfind(close);
            if (end != std::string_view::npos)
                v = v.substr(0, end);
        }
        return std::string(sv_trim(v));
    };

    auto assign = [&](std::string_view key, std::string value) {
        if      (key == "PackageName")             { flush(); current.name = std::move(value); in_pkg = true; }
        else if (key == "PackageVersion")          current.version          = std::move(value);
        else if (key == "PackageLicenseConcluded") current.license          = std::move(value);
        else if (key == "PackageDownloadLocation") current.download_location= std::move(value);
        else if (key == "PackageCopyrightText")    current.copyright        = std::move(value);
    };

    // Iterate over lines
    std::string_view remaining = text;
    while (!remaining.empty()) {
        auto nl = remaining.find('\n');
        std::string_view line = sv_trim(
            nl != std::string_view::npos ? remaining.substr(0, nl) : remaining);
        remaining = (nl != std::string_view::npos) ? remaining.substr(nl + 1) : std::string_view{};

        if (in_multiline) {
            auto end = line.find("</text>");
            if (end != std::string_view::npos) {
                ml_accum += std::string(sv_trim(line.substr(0, end)));
                assign(ml_key, std::move(ml_accum));
                in_multiline = false;
                ml_key.clear();
                ml_accum.clear();
            } else {
                ml_accum += std::string(line) + "\n";
            }
            continue;
        }

        if (line.empty() || line[0] == '#') continue;

        auto colon = line.find(':');
        if (colon == std::string_view::npos) continue;

        std::string_view key = sv_trim(line.substr(0, colon));
        std::string_view val = sv_trim(line.substr(colon + 1));

        if (sv_starts_with(val, "<text>")) {
            // Check whether </text> closes on the same line
            auto close = val.find("</text>", 6);
            if (close != std::string_view::npos) {
                assign(key, strip_text_tags(val));
            } else {
                in_multiline = true;
                ml_key       = std::string(key);
                ml_accum     = std::string(val.substr(6)) + "\n"; // skip <text>
            }
        } else {
            assign(key, std::string(val));
        }
    }
    flush();
}

// ---------------------------------------------------------------------------
// rendering
// ---------------------------------------------------------------------------

void spdx_table::draw()
{
    // Mode toggle
    ImGui::RadioButton("Table", &_mode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Raw",   &_mode, 1);

    ImGui::Spacing();

    if (_mode == 0)
        _draw_table();
    else
        _draw_raw();
}

void spdx_table::_draw_table()
{
    if (_packages.empty()) {
        ImGui::TextDisabled("(no packages parsed)");
        return;
    }

    constexpr ImGuiTableFlags flags =
        ImGuiTableFlags_ScrollX     |
        ImGuiTableFlags_ScrollY     |
        ImGuiTableFlags_BordersOuter|
        ImGuiTableFlags_BordersInnerV|
        ImGuiTableFlags_RowBg       |
        ImGuiTableFlags_SizingFixedFit;

    if (!ImGui::BeginTable("##spdx_pkgs", 4, flags, ImVec2(640, 320)))
        return;

    ImGui::TableSetupScrollFreeze(1, 1); // freeze first column + header row
    ImGui::TableSetupColumn("Package",          ImGuiTableColumnFlags_WidthFixed, 160.f);
    ImGui::TableSetupColumn("Version",          ImGuiTableColumnFlags_WidthFixed, 110.f);
    ImGui::TableSetupColumn("License",          ImGuiTableColumnFlags_WidthFixed, 190.f);
    ImGui::TableSetupColumn("URL",              ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    for (const auto& pkg : _packages) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(pkg.name.c_str());
        ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(pkg.version.c_str());
        ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(pkg.license.c_str());
        ImGui::TableSetColumnIndex(3);
        if (!pkg.download_location.empty() && pkg.download_location != "NOASSERTION")
            ImGui::TextLinkOpenURL(pkg.download_location.c_str(), pkg.download_location.c_str());
        else
            ImGui::TextDisabled("%s", pkg.download_location.c_str());
    }

    ImGui::EndTable();
}

void spdx_table::_draw_raw()
{
    ImGui::BeginChild("##spdx_raw", ImVec2(640, 320), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted(_raw.c_str());
    ImGui::EndChild();
}
