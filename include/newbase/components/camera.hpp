#pragma once

namespace nb {

struct ccamera {
    float zoom  { 1.0f };
    float near_z { -1000.0f };
    float far_z  {  1000.0f };
    // World-space extents hint used by cam_2d_setup and get_2d_extents.
    // 0 means "use zoom directly".
    float wmax  { 0.f };
    float hmax  { 0.f };

    static void _ensure_rtti();
};

}
