#pragma once

#include <newbase/utility/glm.hpp>
#include <box2d/box2d.h>

namespace nb
{
    void physics2d_setup_debug_draw(b2DebugDraw &draw, void*context);
    void physics2d_pre_debug_draw(b2DebugDraw &draw, float cx, float cy, float world_scale, float ui_scale, glm::vec4 world_bounds, glm::vec4 ui_vp);
    void physics2d_post_debug_draw();
}
