#pragma once

#include <cstdint>
#include <vector>
#include <entt/entt.hpp>
#include <newbase/services/renderer_service.hpp>

namespace nb {

// A render layer describes one rendering pass:
//   - which scene to draw from (0 = default scene)
//   - which entity layer bits to include (entities whose clayers::mask & layer_mask != 0 are drawn;
//     entities with no clayers component are always drawn)
//   - which camera entity to use (must carry ccamera + cspatial in that scene)
//   - which viewport to render into
//   - execution order (ascending)
struct render_layer {
    entt::id_type    scene_id    { 0 };
    uint32_t         layer_mask  { 0xFFFFFFFF };
    entt::entity     camera      { entt::null };
    viewport_handle  viewport    { VIEWPORT_INVALID };
    int              order       { 0 };
    bool             clear_bg    { true };
    float            clear_r     { 0.f };
    float            clear_g     { 0.f };
    float            clear_b     { 0.f };
    bool             use_grid    { false }; // TODO: replace with proper grid layer/component
};

} // namespace nb
