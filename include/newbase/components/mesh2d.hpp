#pragma once

#include <newbase/geom/geometry_buffer_2d.hpp>
#include <newbase/res/texture.hpp>
#include <memory>

namespace nb {

struct cmesh2d {
    std::shared_ptr<geometry_buffer_2d> geom;
    std::shared_ptr<rtexture>           tex;   // nullptr = untextured (vertex colors only)
    bool                                visible {true};

    static void _ensure_rtti();
};

} // namespace nb
