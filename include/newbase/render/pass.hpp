#pragma once

#include <newbase/render/texture.hpp>
#include <newbase/render/types.hpp>
#include <array>
#include <cstdint>

namespace nb { class rtexture; }

// render target information for a render pass

// used for pipeline differentiation by the 3D command collector
// ideally also used by the renderer to drive its own render pass target setup
// matching collector pipeline ids 1:1

namespace nb::render
{

static constexpr int PASS_MAX_COLOR_TARGETS = 4;

// Target properties that affect graphics pipeline compatibility.
struct pipeline_target_info
{
    std::array<texture_format, PASS_MAX_COLOR_TARGETS> color_targets {texture_format::INVALID};
    texture_format depth_target {texture_format::INVALID};
    texture_sample_count samples {texture_sample_count::SAMPLES_1};
};

// Full render-pass target information, including the selected attachment
// subresources. Subresource selection does not affect pipeline compatibility.
struct pass_target_info : pipeline_target_info
{
    // per-color-target subresource selection: which mip level and which layer/face
    // to bind when the target refers to an array texture or cubemap.
    // Defaults to 0 for first-level / base layer.
    std::array<uint32_t, PASS_MAX_COLOR_TARGETS> color_mip_level {0,0,0,0};
    std::array<uint32_t, PASS_MAX_COLOR_TARGETS> color_layer_index {0,0,0,0};

    uint32_t depth_mip_level {0};
    uint32_t depth_layer_index {0};
};

// a render pass type
enum class pass_type: uint8_t
{
    INVALID,
    FORWARD,
    EQUIRECTANGULAR_TO_CUBEMAP,
    DIFFUSE_IRRADIANCE,
    SPECULAR_PREFILTER
};

/// A pass can target either a renderer-managed target (including the default
/// swapchain target) or explicit texture attachments. Explicit texture pointers
/// must remain valid through collection and rendering.
struct pass_target
{
    target_id_t managed_target {TARGET_INVALID};
    std::array<rtexture*, PASS_MAX_COLOR_TARGETS> color_textures {};
    rtexture *depth_texture {nullptr};

    bool uses_managed_target() const { return managed_target != TARGET_INVALID; }
};

/// Describes one ordered graphics pass within a render layer.
struct render_pass
{
    pass_type type {pass_type::INVALID};
    pass_target target {};
    pass_target_info targets {};
    bool clear_color {false};
    bool generate_mipmaps_after_render {false};
};

}
