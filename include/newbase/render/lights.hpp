#pragma once
#include "glm/ext/vector_uint4.hpp"
#include <newbase/utility/glm.hpp>
#include <newbase/geom/aabb.hpp>


namespace nb
{
    // ecs components
    class clight;
    class cspatial;
}

namespace nb::render
{
    static constexpr size_t SCENE_LIGHTS_MAX = 32;
    static constexpr size_t MODEL_LIGHTS_MAX = 8;

    /// light data entry as passed to gpu
    struct light_uniform_entry {
        // xyz = World Position (Point/Spot) or Direction (Directional)
        // w   = Light Type (0 = Directional, 1 = Point, 2 = Spot)
        glm::vec4 position_or_direction;

        glm::vec4 spot_direction; // only used for spot, w is padding

        // rgb = Color * Intensity
        // w   = Range (Point/Spot)
        glm::vec4 color_intensity;

        // x = Linear attenuation
        // y = Quadratic attenuation
        // z = Inner cone angle cosine (Spot)
        // w = Outer cone angle cosine (Spot)
        glm::vec4 params;
    };

    /// light data block as passed to the gpu
    struct light_uniform_block
    {
        glm::vec3 ambient {0.0f};  // ignored for ibl
        float _pad0 {0.0f};
        std::array<light_uniform_entry, SCENE_LIGHTS_MAX> lights;
    };

    /// light assignments for a model
    /// to be added to model uniforms
    struct light_model_uniform_block
    {
        uint32_t light_count;
        float _pad0[3];
        std::array<glm::uvec4, MODEL_LIGHTS_MAX/4> packed_light_indices;
    };

    /// takes a light component and prepares the uniform data entry for it
    /// a spatial component is optional
    /// directional lights are transformed as pointing towards -y
    void light_uniform_setup(light_uniform_entry &dst, const clight &light, const cspatial *spatial = nullptr);



    /// light culler
    /// TODO rework this into a per-render-layer light culler
    /// - initialize with viewproj
    /// - go over all lights in scene, calc aabb, and cull with frustum
    /// later, for each mesh+spatial:
    /// - each light: intersect aabb with model aabb
    ///     - if overlap, add to candidate list
    /// - sort candidates by dist sq, IF more candidates thax max light per model
    /// - pick N-closest lights up to max lights per model
    /// - write indices in given list
    /// TODO in even more distant future: handle shadow map assignments and stuff

    struct light_culler
    {

        light_culler(const aabb::frustum_culler &culler) : m_frustum_culler(culler)
        {
            m_culled_lights.reserve(SCENE_LIGHTS_MAX);
            m_candidates.reserve(SCENE_LIGHTS_MAX);
        }

        /// culls a light agains the viewproj
        /// adds it to internal light list
        /// @returns true if light was accepted, false if it was rejected by culling
        bool add_light(const clight &light, const cspatial *spatial = nullptr);

        /// writes the culled scene light data to the scene uniform
        /// @returns the number of active scene lights
        int write_culled_lights(light_uniform_block &ublock);

        /// tests a model aabb against the culled lights
        /// selects the neaarest lights, and writes their indices to the model uniform
        /// @returns the number of scene light affecting the model
        int write_culled_model_lights(light_model_uniform_block &mublock, const aabb& model_world_aabb);

    private:
        // internal structure that keeps track of a light
        struct light_entry
        {
            clight const *light;
            aabb world_aabb;
            bool positional;
            glm::vec3 pos_center;
            light_uniform_entry data;
        };

        // internal structure for potential assignments for scene lights to a model
        struct model_light_candidate
        {
            uint32_t idx;
            float dist2;
        };

        const aabb::frustum_culler &m_frustum_culler;
        std::vector<light_entry> m_culled_lights;
        std::vector<model_light_candidate> m_candidates;
    };



}
