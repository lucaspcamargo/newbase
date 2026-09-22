#pragma once

#include "newbase/render/types.hpp"
#include <newbase/render/target.hpp>
#include <newbase/res/texture.hpp>


namespace nb
{
    /// Private render_2d structure for a render target
    /// Rule of zero applies here
    struct render_2d_target
    {
        render::target_id_t id {render::TARGET_INVALID};
        render::target_desc desc {};
        std::shared_ptr<rtexture> color_tex {nullptr};
        int curr_w {-1};
        int curr_h {-1};

        /// returns the render target id that this target's size depends on
        /// TARGET_INVALID means it has a fixed, independent size
        /// TARGET_DEFAULT means it depends on the main window (or UI central node)
        inline render::target_id_t size_dep()
        {
            return (desc.size_mode == render::target_size_mode::TARGET_RELATIVE)
                ? desc.size_source
                : (desc.size_mode == render::target_size_mode::UI_RELATIVE
                   ? render::TARGET_DEFAULT
                   : render::TARGET_INVALID
                  );
        }
    };
}
