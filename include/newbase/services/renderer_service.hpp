#pragma once

#include <newbase/render/types.hpp>
#include <newbase/res/texture.hpp>
#include <newbase/utility/glm.hpp>


namespace nb
{

/// Service provided by the active renderer. Used by engine tooling to query
/// window geometry and to create/update backend-agnostic textures (e.g. for the resource editor).

class renderer_service
{
public:

    virtual ~renderer_service() = default;

    virtual int   window_width()  const { return 0; }
    virtual int   window_height() const { return 0; }
    virtual float display_scale() const { return 1.f; }  // TODO rename to ui_scale, add event_scale

    // --- UI texture helpers ---

    // Opaque handle to a renderer-managed texture. The concrete type is
    // backend-specific; callers must not assume anything about its value.
    // It is also a valid ImTextureID (cast is safe on all supported backends).
    using texture_handle = void*;

    // Create an updatable (streaming) RGBA texture of the given pixel dimensions.
    // The caller owns the handle and must call destroy_texture when done.
    virtual texture_handle create_texture(int w, int h) = 0;

    // Push a full-image CPU pixel buffer into the texture.
    // pixels must point to w*h RGBA bytes; pitch is the row stride in bytes.
    virtual void update_texture(texture_handle tex, const void* pixels, int pitch) = 0;

    // Free a texture previously created with create_texture.
    virtual void destroy_texture(texture_handle tex) = 0;


    // --- Render Target Management ---
    // This interface uses handles to create, destroy and represent render targets.
    // They are internally managed by the renderer.
    // NOTE: it may be better to use render::target_ref to create and manage
    //       the target, usually within a shared_ptr

    virtual render::target_id_t target_create(const render::target_desc& desc) = 0;
    virtual bool target_destroy(render::target_id_t id) = 0;
    virtual std::shared_ptr<rtexture> target_get_color_texture(render::target_id_t id) const = 0;
    virtual std::shared_ptr<rtexture> target_get_depth_texture(render::target_id_t id) const = 0;
    virtual glm::ivec2     target_get_size(render::target_id_t id) const = 0;
    virtual bool           target_has_depth(render::target_id_t id) const = 0;

};

} // namespace nb
