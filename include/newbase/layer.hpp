#pragma once

#include <cstdint>
#include <entt/entt.hpp>
#include <newbase/render/types.hpp>
#include <newbase/render/batcher2d.hpp>
#include <functional>

namespace nb {

/// A render_layer describes one rendering "operation", that draws a scene
/// (or optionally, custom 2d geometry) into a render target. Common
/// rendering properties such as clearing, the camera to use, and
/// render order are also described here.
///
/// TODO: move to render namespace?

struct render_layer
{
    /// Id of the scene to render. Our huge multiscene refactor is still pending.
    entt::id_type       scene_id    { 0 };

    /// Layer mask. Will be used to exclude entities with a clayer component.
    uint32_t            layer_mask  { 0xFFFFFFFF };

    /// Camera to be used for rendering. Together with a viewport, it defines
    /// the view projection matrix for the layer.
    entt::entity        camera      { entt::null };

    /// The rendering order for this layer. Layers are sorted by the engine
    /// according to this.
    int                 order       { 0 };

    /// The vewport to use for rendering this layer.
    render::viewport_t  viewport    {};

    /// Whether to automatically update this layer's viewport to match the UI's
    /// central view node.
    bool                follow_ui   {false};

    /// Whether UI overlays render over this layer
    /// Note that layers with render targets will always use the main UI viewport
    bool                ui_overlays {false};

    /// Render target to draw this layer onto. The targets are managed by the
    /// renderer via the renderer_service interface.
    render::target_id_t target_id   {render::TARGET_DEFAULT};

    /// Whether to resize viewport to render target size (except for TARGET_DEFAULT)
    bool                follow_target {false};

    /// Whether to cear the target viewport before rendering.
    bool                clear       { true };

    /// Red channel of the clear color. Unused if clear is false.
    float               clear_r     { 0.f };

    /// Green channel of the clear color. Unused if clear is false.
    float               clear_g     { 0.f };

    /// Blue channel of the clear color. Unused if clear is false.
    float               clear_b     { 0.f };

    /// The type of the custom 2d drawing function.
    /// A callback that is supplied the layer and a batcher reference.
    /// The custom drawing function is expected to fill the batcher with custom
    /// 2D geometry and commands, for rendering.
    using custom_2d_draw_t = std::function<void(const render_layer &l, render::batcher2d &batcher)>;

    /// An optional custom 2D drawing callback to use for this layer.
    /// When present, it overrides the normal scene drawing with the
    /// 2D geometry produced by the callback.
    custom_2d_draw_t custom_2d_draw {};
};

} // namespace nb
