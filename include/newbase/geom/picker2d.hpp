#pragma once

#include <newbase/layer.hpp>

namespace nb::geom
{

/**
 * Our 2D picking implementation, on the CPU side.
 * It is currently stateless, but this might change in the future.
 * It does not implement the picker service, as the renderer is responsible for
 * delegating that function to the appropriate implementation in case it
 * implements GPU picking. This is just one possible backend.
 */
class picker_2d
{
public:
    static entt::entity pick(const render_layer &layer, float vp_x, float vp_y);

};

}
