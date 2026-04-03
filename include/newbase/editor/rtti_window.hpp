#pragma once

#include <string>

namespace nb {

class rtti_window {
public:
    /** Render the RTTI dump window. *p_open is set to false when the user closes it. */
    void draw(bool* p_open);

private:
    void _ensure_loaded();

    std::string _rtti_text;
    bool        _loaded = false;
};

} // namespace nb