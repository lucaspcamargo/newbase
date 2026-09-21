#pragma once

#include <cstdint>
#include <entt/entt.hpp>
#include <newbase/render/types.hpp>
#include <newbase/render/batcher2d.hpp>
#include <functional>

namespace nb {

// A render layer describes one rendering pass:
//   - which scene to draw from (0 = default scene)
//   - which entity layer bits to include (entities whose clayers::mask & layer_mask != 0 are drawn;
//     entities with no clayers component are always drawn)
//   - which camera entity to use (must carry ccamera + cspatial in that scene)
//   - the viewport to use
//   - execution order (ascending)
//   - whether it must be updated to follow UI (central dock node)

struct render_layer {
    entt::id_type    scene_id    { 0 };
    uint32_t         layer_mask  { 0xFFFFFFFF };
    entt::entity     camera      { entt::null };
    render::viewport_t viewport  { };
    int              order       { 0 };
    bool             follow_ui   {false};
    bool             clear       { true };
    float            clear_r     { 0.f };
    float            clear_g     { 0.f };
    float            clear_b     { 0.f };

    using custom_2d_draw_t = std::function<void(const render_layer &l, render::batcher2d &batcher)>;
    custom_2d_draw_t custom_2d_draw {};
};

} // namespace nb
