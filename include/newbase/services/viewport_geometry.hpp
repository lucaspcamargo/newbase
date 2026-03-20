#pragma once

namespace nb
{

/// This service is used by engine tooling, to determine hwo to render debug overlays
/// onto the scene.
/// Currently used by physics debug draw. To be used by editor picking and gizmos too.   

class viewport_geometry
{
public:
    struct extents_2d
    {
        int width;      // viewport size in pixels
        int height;     // viewport size in pixels

        float xspan;    // how wide the viewport is in world units
        float yspan;    // how tall the viewport is in world units
        
        float left;     // left border x, in world coordinates
        float top;      // top border y, in world coordinates
        float right;    // right border x, in world coordinates
        float bottom;   // bottom border y, in world coordinates

        float ui_scale; // how much UI drawing will be scaled by, when using ImGui to draw
    };

    virtual ~viewport_geometry() = default;
    virtual bool get_2d_extents(extents_2d &) = 0;
};

}