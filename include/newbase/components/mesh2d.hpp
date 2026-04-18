#pragma once

#include <newbase/geom/geometry_buffer_2d.hpp>
#include <newbase/res/texture.hpp>
#include <memory>

namespace nb {

enum class blend_mode_2d { ALPHA, ADD };

struct cmesh2d {
    std::shared_ptr<geometry_buffer_2d> geom;
    std::shared_ptr<rtexture>           tex;        // nullptr = untextured (vertex colors only)
    blend_mode_2d                       blend_mode  { blend_mode_2d::ALPHA };
    bool                                visible     { true };

    static void _ensure_rtti();
};

} // namespace nb
