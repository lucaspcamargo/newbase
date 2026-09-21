#pragma once

#include <newbase/render/camera.hpp>

namespace nb
{

/**
 * This is our camera component.
 *
 * Note that this is about the projection configuration.
 * A corresponding spatial component on the same entity
 * supplies the world transform matrix.
 *
 * The "type" of camera to be used is determined by the render
 * layer, not the data component.
 */
struct ccamera
{
    render::camera_2d cam2d {};
    render::camera_3d cam3d {};

    static void _ensure_rtti();
};

}
