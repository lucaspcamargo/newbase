#pragma once

#include <newbase/res/tilemap.hpp>
#include <memory>
#include <string>

namespace nb {

struct ctilemap {
    std::shared_ptr<rtilemap> map;
    std::string               render_layer;    // layer name to render (empty = all layers)
    std::string               collision_layer; // layer name used for collision (empty = none)
    bool                      visible { true };
    bool                      _built  { false }; // set by tilemap_system after mesh/body setup

    static void _ensure_rtti();
};

} // namespace nb
