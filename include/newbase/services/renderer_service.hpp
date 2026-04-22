#pragma once

#include <cstdint>

namespace nb
{

/// Service provided by the active renderer. Used by engine tooling to query
/// viewport geometry (for debug overlays, gizmos, picking) and to create/update
/// backend-agnostic textures (e.g. for the resource editor).

// Opaque handle to a renderer-managed viewport (window region or texture target).
using viewport_handle = uint32_t;
static constexpr viewport_handle VIEWPORT_INVALID = 0;

class renderer_service
{
public:
    // Opaque handle to a renderer-managed texture. The concrete type is
    // backend-specific; callers must not assume anything about its value.
    // It is also a valid ImTextureID (cast is safe on all supported backends).
    using texture_handle = void*;

    struct extents_2d
    {
        int width;      // viewport size in pixels
        int height;     // viewport size in pixels

        float xspan;    // how wide the viewport is in world units
        float yspan;    // how tall the viewport is in world units

        float left;     // left border x, in world coordinates
        float top;      // top border y, in world coordinates
        float right;    // right border x, in world coordinates
        float bottom;   // bottom border y, in world coordinates

        float ui_scale; // how much UI drawing will be scaled by, when using ImGui to draw

        int screen_x;   // viewport top-left in physical pixels (0 when no docked panels)
        int screen_y;
    };

    virtual ~renderer_service() = default;

    virtual bool get_2d_extents(extents_2d&) = 0;

    // Returns the handle of the "default viewport" — a persistent viewport that
    // covers the window area available for scene rendering.  Callers (e.g. the
    // ui_manager / editor) may call update_viewport() on this handle to confine
    // scene rendering to a sub-region (e.g. the DockSpace central node).
    // reset_default_viewport() restores it to the full window.
    // Returns VIEWPORT_INVALID if the renderer does not support this.
    virtual viewport_handle default_viewport() const { return VIEWPORT_INVALID; }
    virtual void reset_default_viewport() {}

    // --- viewport management ---

    // Create a viewport targeting a region of the window framebuffer.
    // x, y, w, h are in logical pixels. clear_color is RGBA [0..1].
    virtual viewport_handle create_viewport(int x, int y, int w, int h,
                                            bool clear = true,
                                            float r = 0.f, float g = 0.f,
                                            float b = 0.f, float a = 1.f) = 0;

    // Update the pixel region of an existing viewport.
    virtual void update_viewport(viewport_handle vp, int x, int y, int w, int h) = 0;

    virtual void destroy_viewport(viewport_handle vp) = 0;

    // --- texture management ---

    // Create an updatable (streaming) RGBA texture of the given pixel dimensions.
    // The caller owns the handle and must call destroy_texture when done.
    virtual texture_handle create_texture(int w, int h) = 0;

    // Push a full-image CPU pixel buffer into the texture.
    // pixels must point to w*h RGBA bytes; pitch is the row stride in bytes.
    virtual void update_texture(texture_handle tex, const void* pixels, int pitch) = 0;

    // Free a texture previously created with create_texture.
    virtual void destroy_texture(texture_handle tex) = 0;
};

} // namespace nb
