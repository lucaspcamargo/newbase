#pragma once

#include <newbase/utility/glm.hpp>

namespace nb
{
    struct aabb
    {
        // Default to an invalid "inverted" state so the first point/bounds correctly resets it
        glm::vec3 min{ std::numeric_limits<float>::max() };
        glm::vec3 max{ std::numeric_limits<float>::lowest() };

        aabb() = default;
        aabb(const glm::vec3& min_val, const glm::vec3& max_val) : min(min_val), max(max_val) {}

        // --- Core Properties ---

        [[nodiscard]] glm::vec3 get_center() const
        {
            return (min + max) * 0.5f;
        }

        [[nodiscard]] glm::vec3 get_extents() const
        {
            return (max - min) * 0.5f;
        }

        [[nodiscard]] glm::vec3 get_size() const
        {
            return max - min;
        }

        [[nodiscard]] bool is_valid() const
        {
            return min.x <= max.x && min.y <= max.y && min.z <= max.z;
        }

        // mod/accum

        // expand to add a point
        void grow(const glm::vec3& point)
        {
            min = glm::min(min, point);
            max = glm::max(max, point);
        }

        // expand to add another aabb
        void grow(const aabb& other)
        {
            if (!other.is_valid()) return;
            min = glm::min(min, other.min);
            max = glm::max(max, other.max);
        }


        // transformations (local to world space and such)
        [[nodiscard]] aabb transform(const glm::mat4& m) const
        {
            if (!is_valid()) return *this;

            glm::vec3 center = get_center();
            glm::vec3 extents = get_extents();

            glm::vec3 new_center = glm::vec3(m * glm::vec4(center, 1.0f));

            glm::mat3 rot = glm::mat3(m);
            glm::vec3 new_extents = glm::vec3(
                glm::dot(glm::abs(rot[0]), extents),
                                              glm::dot(glm::abs(rot[1]), extents),
                                              glm::dot(glm::abs(rot[2]), extents)
            );

            return aabb{ new_center - new_extents, new_center + new_extents };
        }


        // intersections and tests

        [[nodiscard]] bool contains(const glm::vec3& point) const
        {
            return (point.x >= min.x && point.x <= max.x) &&
            (point.y >= min.y && point.y <= max.y) &&
            (point.z >= min.z && point.z <= max.z);
        }

        [[nodiscard]] bool intersects(const aabb& other) const
        {
            return (min.x <= other.max.x && max.x >= other.min.x) &&
            (min.y <= other.max.y && max.y >= other.min.y) &&
            (min.z <= other.max.z && max.z >= other.min.z);
        }

        [[nodiscard]] bool intersects_sphere(const glm::vec3& sphere_center, float radius) const
        {
            glm::vec3 closest_point = glm::clamp(sphere_center, min, max);
            glm::vec3 diff = sphere_center - closest_point;
            float distance_sq = glm::dot(diff, diff);
            return distance_sq <= (radius * radius);
        }

        /// Gribb-Hartmann helper with cached planes
        /// You can build one with a viewproj matrix and hold onto it for faster
        /// repeated aabb culling
        struct frustum_culler
        {
            explicit frustum_culler(glm::mat4 view_proj)
            {
                // row vectors from the column-major matrix
                glm::vec4 r0(view_proj[0][0], view_proj[1][0], view_proj[2][0], view_proj[3][0]);
                glm::vec4 r1(view_proj[0][1], view_proj[1][1], view_proj[2][1], view_proj[3][1]);
                glm::vec4 r2(view_proj[0][2], view_proj[1][2], view_proj[2][2], view_proj[3][2]);
                glm::vec4 r3(view_proj[0][3], view_proj[1][3], view_proj[2][3], view_proj[3][3]);

                // 6 planes: left, right, bottom, top, near ([0,1] -> z >= 0), far (z <= w)
                m_planes = {
                    r3 + r0, // Left
                    r3 - r0, // Right
                    r3 + r1, // Bottom
                    r3 - r1, // Top
                    r2,      // Near
                    r3 - r2  // Far
                };

                // Normalize the planes so dot products yield true world-space distances
                for (auto& plane : m_planes) {
                    float length = glm::length(glm::vec3(plane));
                    if (length > 0.0f) {
                        plane /= length;
                    }
                }
            }

            bool intersects(const aabb &bb) const
            {
                for (const auto& plane : m_planes) {
                    // Find the "p-vertex" (the corner of the AABB furthest along the plane normal)
                    glm::vec3 normal(plane.x, plane.y, plane.z);
                    glm::vec3 p = bb.min;
                    if (normal.x >= 0.0f) p.x = bb.max.x;
                    if (normal.y >= 0.0f) p.y = bb.max.y;
                    if (normal.z >= 0.0f) p.z = bb.max.z;

                    // If the p-vertex is behind the plane, the entire AABB is outside
                    if ((glm::dot(normal, p) + plane.w) < 0.0f) {
                        return false;
                    }
                }

                return true;
            }

            std::array <glm::vec4, 6> m_planes;
        };

        [[nodiscard]] bool intersects_frustum(const glm::mat4& view_proj) const
        {
            // the Gribb-Hartmann method

            if (!is_valid()) return false;

            return frustum_culler{view_proj}.intersects(*this);
        }
    };
}
