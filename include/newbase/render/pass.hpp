#pragma once

#include <newbase/render/texture.hpp>
#include <array>

// render target information for a render pass

// used for pipeline differentiation by the 3D command collector
// ideally also used by the renderer to drive its own render pass target setup
// matching collector pipeline ids 1:1

namespace nb::render
{

static constexpr int PASS_MAX_COLOR_TARGETS = 4;

// the target information for a render pass
// used for describing a pipeline target
// an INVALID texture format means no target is bound there
struct pass_target_info
{
    std::array<texture_format, PASS_MAX_COLOR_TARGETS> color_targets {texture_format::INVALID};
    texture_format depth_target {texture_format::INVALID};
    texture_sample_count samples {texture_sample_count::SAMPLES_1};
};

// a reder pass type
enum class pass_type: uint8_t
{
    INVALID,
    FORWARD
};

}
