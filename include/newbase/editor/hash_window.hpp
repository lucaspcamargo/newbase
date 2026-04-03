#pragma once

#include <string>

namespace nb {

class hash_window {
public:
    /** Render the hash calculator window. *p_open is set to false when the user closes it. */
    void draw(bool* p_open);

private:
    std::string _input = "rsprite";
    char _input_buf[256] = "rsprite";
};

} // namespace nb