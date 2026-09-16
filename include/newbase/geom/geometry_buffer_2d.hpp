#pragma once

#include <newbase/utility/glm.hpp>
#include <newbase/render/batcher2d.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <vector>

namespace nb {

// CPU-side 2D geometry buffer. No renderer dependency, just plain data.
// Can be used by static geometry resources and dynamic per-frame geometry.
// We used to roll our own vertex type, but now we reuse it from batcher2d.
// We may rework (or get rid of) this when we have more general ways to store and represent geometry.

struct geometry_buffer_2d
{
    using vertex = render::vertex2d;

    std::vector<vertex> vertices;
    std::vector<uint16_t>    indices;  // empty = vertices drawn as sequential triangles

    void clear() { vertices.clear(); indices.clear(); }
    bool empty() const { return vertices.empty(); }

    // Append a quad (two triangles), corners: top-left, top-right, bottom-left, bottom-right.
    void push_quad(vertex tl, vertex tr, vertex bl, vertex br)
    {
        const uint16_t base = static_cast<uint16_t>(vertices.size());
        vertices.push_back(tl);
        vertices.push_back(tr);
        vertices.push_back(bl);
        vertices.push_back(br);
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 1);
        indices.push_back(base + 3);
        indices.push_back(base + 2);
    }
};

} // namespace nb
