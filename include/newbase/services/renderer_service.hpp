#pragma once

namespace nb
{

/// Service provided by the active renderer. Used by engine tooling to query
/// viewport geometry (for debug overlays, gizmos, picking) and to create/update
/// backend-agnostic textures (e.g. for the resource editor).

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
    };

    virtual ~renderer_service() = default;

    virtual bool get_2d_extents(extents_2d&) = 0;

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
