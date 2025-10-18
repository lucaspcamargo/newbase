#pragma once

#include <box2d/box2d.h>

namespace nb
{
    void physics2d_setup_debug_draw(b2DebugDraw &draw, void*context);
    void physics2d_pre_debug_draw(b2DebugDraw &draw, float dx, float dy, float sx, float sy, float world_scale, float ui_scale);
}