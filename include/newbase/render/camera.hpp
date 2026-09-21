#pragma once

#include <newbase/render/types.hpp>
#include <newbase/utility/glm.hpp>

namespace nb::render
{
    // CAMERAS

    /**
     *  Fit mode for the 2d camera
     *  Fitting can override the scale to ensure that a given worldspace area
     *  is covered by the camera, or the camera covers the entire area
     */
    enum class camera_2d_fit: int
    {
        /// just use zoom directly
        NO_FIT,

        /// camera fully contained within bounds, camera can be smaller than bounds
        FIT_MIN,

        /// bounds fully contained within camera, camera can be larger than bounds
        FIT_MAX,

        /// use fit bounds directly, even if it distorts the output
        FIT_STRETCH
    };

    /**
     * Our 2D camera data
     * It covers a given world area according to viewport dimensions, scale, and fit_mode
     * The fit_mode can override the effective scale by comparing the viewport dimension with the fit bounds
     */
    struct camera_2d
    {
        /**
         * The prescribed zoom level
         * Can be overriden by the fit mode, depending on the viewport
         */
        float scale {1.0f};

        /**
         * The camera's fit mode
         * Can override the scale, depending on the viewport
         */
        camera_2d_fit fit_mode {camera_2d_fit::NO_FIT};

        /**
         * Width of the fit dimensions, in world space
         */
        glm::vec2 fit_world_dims {-1.0f, -1.0f};

        /**
         * Anchor factor for camera fit, in x axis. From 0.0 to 1.0
         * 0.5 means anchor in center
         */
        glm::vec2 fit_anchor {0.5f, 0.5f};


        // Constants

        static constexpr glm::vec4 CAM_WORLD_BOUNDS_INVALID = glm::vec4{0.0};

        // Functions

        /**
         * Calculates camera world bounds (x,y,w,h) for the given viewport, respecting fit mode
         * @param cam_x Camera X center position. From the spatial component.
         * @param cam_y Camera Y center position. From the spatial component.
         * @param cam The camera_2d data to use
         * @param vp The viewport to use
         * @return A glm::vec4 with (x,y,w,h) of the area covered by the camera, in world coordinates, or CAM_WORLD_BOUNDS_INVALID if so
         */
        glm::vec4 calc_world_bounds(float cam_x, float cam_y, const viewport_t& vp) const;
    };

    /**
     * Our 3D camera data
     * Most of the projection data is derived from the spatial component
     * of the camera entity, and the viewport bounds, so all we need to specify
     * is the FOV, and clip plane distances.
     * We have chosen here some general-purpose defaults
     */
    struct camera_3d
    {
        /// Camera Field-of-View angle, in radians
        float fov {static_cast<float>(M_PI*2.4f)};

        // Near clip distance
        float clip_near {0.5f};

        // Far clip distance
        float clip_far {500.0f};

    };



}
