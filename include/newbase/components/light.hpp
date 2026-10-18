#pragma once
#include <newbase/geom/aabb.hpp>
#include <glm/glm.hpp>
#include <cstdint>

namespace nb {

enum class light_type : uint8_t {
    DIRECTIONAL,
    POINT,
    SPOT
};

struct clight {
    light_type type {light_type::POINT};

    // common props
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;

    // attenuation and reach
    float range = 10.0f;
    float linear = 0.09f;
    float quadratic = 0.032f;

    // cone angles (for spot), in radians
    float inner_cone_angle = 0.523599f; // ~30 deg
    float outer_cone_angle = 0.785398f; // ~45 deg

    // control flags
    bool casts_shadows = false;

    static void _ensure_rtti();

    [[nodiscard]] inline aabb get_bounds(const glm::vec3& position, const glm::vec3& direction = glm::vec3(0.0f, -1.0f, 0.0f)) const
    {
        switch (type) {
            case light_type::POINT: {
                // AABB of a sphere. Wow. Amazing.
                return aabb{
                    position - glm::vec3(range),
                    position + glm::vec3(range)
                };
            }
            case light_type::SPOT: {
                // this could be better for sure...
                // but its ok

                // find center of cone base
                glm::vec3 base_center = position + direction * range;

                // calc base radius
                float base_radius = range * std::tan(outer_cone_angle);

                // start at apex and grow towards base center
                aabb box{ position, position };
                box.grow(base_center);

                // expand by base radius to cover whole cone
                box.min -= glm::vec3(base_radius);
                box.max += glm::vec3(base_radius);

                return box;
            }
            case light_type::DIRECTIONAL:
            default: {
                // infinite :)
                constexpr float inf = std::numeric_limits<float>::max();
                return aabb{ glm::vec3(-inf), glm::vec3(inf) };
            }
        }
    }
};

}
