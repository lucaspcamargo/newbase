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
        int width;
        int height;
        
        float left;
        float top;
        float right;
        float bottom;
    };

    virtual bool get_2d_extents(extents_2d &) = 0;
};

}