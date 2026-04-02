#pragma once

#include <newbase/editor/spdx_table.hpp>

namespace nb {

class about_window {
public:
    /** Render the about dialog. *p_open is set to false when the user closes it. */
    void draw(bool* p_open);

private:
    void _ensure_loaded();

    spdx_table _sbom;
    bool       _loaded = false;
};

} // namespace nb
