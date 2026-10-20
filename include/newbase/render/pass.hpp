#pragma once

#include <newbase/render/texture.hpp>
#include <newbase/render/types.hpp>
#include <array>
#include <cstdint>
#include <memory>

namespace nb
{
    class rtexture;
    class rmaterial;
    class rmesh;
    class scene;
    struct render_layer;
}

// Engine-native render pass and render target information constructs.
// Also describes the target-dependent formats that inform pipeline
// building, extending it with details used build render passes and binds
// the actual target textures and slices.

// TODO this is currently a bit messy
//      perhaps we should drop the "managed" part from pass_target
//      and also make target management in render_gpu just use target
//      textures with standard texture management via prepare(), and drop
//      the separate texture preparation codepath.
//      We shall try to us a standard target manager in render_gles and then
//      move render_gpu and render_2d to it, dripping the duplicate sizing
//      code. (to be fair, this is more abut targets than passes :)


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

/// Input describing the scene and layer for which a builder may add work.
struct pass_build_request
{
    const render_layer &layer;
    nb::scene &scene;
    pass_target_info layer_targets;
};

enum class build_result : uint8_t
{
    SUCCESS,
    NOT_APPLICABLE,
    FAILURE
};

// a render pass type
enum class pass_type: uint8_t
{
    INVALID,
    FORWARD,
    EQUIRECTANGULAR_TO_CUBEMAP,
    DIFFUSE_IRRADIANCE,
    SPECULAR_PREFILTER,
    DEPTH_ONLY,
    DEPTH_ONLY_SHADOW_MAP
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
    viewport_t viewport {VIEWPORT_DEFAULT};
    bool clear_color {false};
    bool generate_mipmaps_after_render {false};
};

/// Collector-owned operations available to pass builders.
class pass_build_context
{
public:
    virtual ~pass_build_context() = default;

    virtual bool append_draw_pass(const render_pass &pass,
                                  std::shared_ptr<rmaterial> material,
                                  std::shared_ptr<rmesh> mesh,
                                  const void *scene_uniforms = nullptr,
                                  size_t scene_uniforms_size = 0) = 0;
};

/// Builds ordinary render work in response to a resource need.
class pass_builder
{
public:
    virtual ~pass_builder() = default;

    virtual build_result build(pass_build_context &context,
                               const pass_build_request &request) = 0;
};

}
