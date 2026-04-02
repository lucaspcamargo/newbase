#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace nb {

/**
 * Parses an SPDX 2.3 tag-value document and renders it either as a
 * scrollable table (one row per package) or as raw text.
 * Call load() once with the file contents, then draw() every frame.
 */
class spdx_table {
public:
    void load(std::string_view raw_text);
    bool empty() const { return _raw.empty(); }

    /** Renders the content area (no ImGui window or tree node wrapper). */
    void draw();

private:
    struct package {
        std::string name;
        std::string version;
        std::string license;
        std::string download_location;
        std::string copyright;
    };

    void _parse(std::string_view text);
    void _draw_table();
    void _draw_raw();

    std::string          _raw;
    std::vector<package> _packages;
    int                  _mode = 0; // 0 = table, 1 = raw
};

} // namespace nb
