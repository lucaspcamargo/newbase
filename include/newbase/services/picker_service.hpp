#pragma once

#include <newbase/layer.hpp>
#include <entt/entt.hpp>

namespace nb
{

/// Service provided by the active renderer for spatial picking.
/// Coordinates passed to pick() are relative to the viewport's top-left corner,
/// NOT global window or render-target coordinates.
class picker_service
{
public:
    virtual ~picker_service() = default;

    /// Returns the topmost entity at (vp_x, vp_y) within the given render layer,
    /// or entt::null if nothing was hit.
    /// vp_x, vp_y: position in pixels relative to the layer's viewport origin.
    virtual entt::entity pick(const render_layer &layer,
                              float vp_x, float vp_y) = 0;
};

} // namespace nb
