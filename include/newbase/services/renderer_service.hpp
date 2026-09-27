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
