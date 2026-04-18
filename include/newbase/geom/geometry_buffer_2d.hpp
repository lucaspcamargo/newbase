#pragma once

#include <newbase/utility/glm.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <vector>
#include <cstdint>

namespace nb {

// CPU-side 2D geometry buffer. No renderer dependency — plain data.
// Can be used by static geometry resources and dynamic per-frame geometry.
struct geometry_buffer_2d
{
    struct vertex {
        glm::vec2 pos;
        glm::vec2 uv;
        glm::vec4 color {1.f, 1.f, 1.f, 1.f};
    };

    std::vector<vertex> vertices;
    std::vector<int>    indices;  // empty = vertices drawn as sequential triangles

    void clear() { vertices.clear(); indices.clear(); }
    bool empty() const { return vertices.empty(); }

    // Append a quad (two triangles), corners: top-left, top-right, bottom-left, bottom-right.
    void push_quad(vertex tl, vertex tr, vertex bl, vertex br)
    {
        const int base = static_cast<int>(vertices.size());
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
