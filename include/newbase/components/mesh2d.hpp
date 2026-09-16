#pragma once

#include "newbase/render/batcher2d.hpp"
#include <newbase/geom/geometry_buffer_2d.hpp>
#include <newbase/res/texture.hpp>
#include <memory>

namespace nb {

struct cmesh2d {
    std::shared_ptr<geometry_buffer_2d> geom;
    std::shared_ptr<rtexture>           tex;        // nullptr = untextured (vertex colors only)
    render::blendmode2d                 blend_mode  { render::blendmode2d::BLEND };
    bool                                visible     { true };
    bool                                pixel_snap  { false };

    static void _ensure_rtti();
};

} // namespace nb
