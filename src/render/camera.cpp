#include <newbase/render/camera.hpp>

namespace nb::render
{

    glm::vec4 camera_2d::calc_world_bounds(float cam_x, float cam_y, const viewport_t& vp) const
    {
        const auto &cam = *this;
        // TODO fit anchor
        //      calculate what's left of the axis that is not fully covered,
        //      in world space, and move resuting bounds around using world_offset

        if(vp.w <= 0 || vp.h <= 0)
            return CAM_WORLD_BOUNDS_INVALID;

        if(cam.fit_mode == camera_2d_fit::FIT_STRETCH)
        {
            // just use the fit dimensions as the camera rect, around the camera center
            const glm::vec2 fit_dims_h {cam.fit_world_dims * 0.5f};
            return glm::vec4 {cam_x - fit_dims_h.x, cam_y - fit_dims_h.y,
                cam.fit_world_dims.x, cam.fit_world_dims.y};
        }

        float scale {cam.scale};
        glm::vec2 world_offset {0.0f};

        switch(cam.fit_mode)
        {
            case camera_2d_fit::FIT_MIN:
            {
                if(cam.fit_world_dims.x <= 0.0 || cam.fit_world_dims.y <= 0.0)
                    return CAM_WORLD_BOUNDS_INVALID;
                float scale_x =  static_cast<float>(vp.w) / cam.fit_world_dims.x;
                float scale_y = static_cast<float>(vp.h) / cam.fit_world_dims.y;
                scale = std::max(scale_x, scale_y); // use max scale, show within bounds
            }
            break;

            case camera_2d_fit::FIT_MAX:
            {
                if(cam.fit_world_dims.x <= 0.0 || cam.fit_world_dims.y <= 0.0)
                    return CAM_WORLD_BOUNDS_INVALID;
                float scale_x =  static_cast<float>(vp.w) / cam.fit_world_dims.x;
                float scale_y = static_cast<float>(vp.h) / cam.fit_world_dims.y;
                scale = std::min(scale_x, scale_y); // use min scale, show bound and surrounding area
            }
            break;


            case camera_2d_fit::NO_FIT:
                [[fallthrough]];
            default:
            {
                // we just use the scale directly as it is
                if(cam.scale <= 0.0)
                    return CAM_WORLD_BOUNDS_INVALID;
            }
        }

        // now take calculated scale to derive covered area, and return covered bounds
        const glm::vec2 world_dims { vp.w/scale, vp.h/scale };
        const glm::vec2 world_dims_h = world_dims * 0.5f;
        return glm::vec4 {cam_x-world_dims_h.x, cam_y-world_dims_h.y, world_dims.x, world_dims.y};
    }

}
