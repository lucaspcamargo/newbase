#include <newbase/render/lights.hpp>
#include <newbase/components/light.hpp>
#include <newbase/components/spatial.hpp>
#include <algorithm>

using namespace nb;
using namespace nb::render;

bool light_culler::add_light(const clight &light, const cspatial *spatial)
{
    if(m_culled_lights.size() == SCENE_LIGHTS_MAX)
        return false; // we're full

    // conceivably we could try replacing less important lights (farther from view)
    // for now we just drop excess lights

    auto dir = glm::vec3{.0f, -1.f, .0f};
    auto pos = glm::vec3{.0f};
    if(spatial)
    {
        // take position straight from translation column, and transform
        // direction using a w=0 component
        pos = glm::vec3{spatial->world[3]};
        dir = glm::normalize(glm::vec3(spatial->world * glm::vec4(dir, 0.0f)));
    }

    aabb la {light.get_bounds(dir, pos)};
    if(!m_frustum_culler.intersects(la))
        return false;

    bool is_positional = light.type != light_type::DIRECTIONAL;
    glm::vec4 spot_dir {0.0f};
    if(light.type == light_type::SPOT)
        spot_dir = glm::vec4{glm::normalize(glm::vec3(spatial->world * glm::vec4(dir, 0.0f))), 0.0f};
    m_culled_lights.emplace_back(light_entry{
        .light = &light,
        .world_aabb = la,
        .positional = is_positional,
        .pos_center = pos,
        .data = light_uniform_entry {
            .position_or_direction = glm::vec4{ is_positional? pos : dir, (float) light.type },
            .spot_direction = spot_dir,
            .color_intensity = glm::vec4{ light.color * light.intensity, light.range },
            .params = glm::vec4{ light.linear, light.quadratic, cosf(light.inner_cone_angle), cosf(light.outer_cone_angle) }
        }
    });
    return true;
}

int light_culler::write_culled_lights(light_uniform_block &ublock)
{
    int idx = 0;
    for(const auto &entry: m_culled_lights)
    {
        ublock.lights[idx++] = entry.data;
    }
    return idx;
}

int light_culler::write_culled_model_lights(light_model_uniform_block &mublock, const aabb& model_world_aabb)
{
    m_candidates.clear();

    uint32_t entry_idx = 0;
    for(const auto &entry: m_culled_lights)
    {
        if(entry.world_aabb.intersects(model_world_aabb))
        {
            m_candidates.emplace_back(model_light_candidate{
                .idx = entry_idx,
                .dist2 = entry.positional? glm::distance2(entry.pos_center, model_world_aabb.get_center()) : 0.0f,
            });
        }
        ++entry_idx;
    }

    if(m_candidates.size() > MODEL_LIGHTS_MAX)
    {
        std::sort(m_candidates.begin(), m_candidates.end(), [](const auto& a, const auto& b) {
            return a.dist2 < b.dist2;
        });
    }

    mublock.light_count = 0;

    for(const auto &cand: m_candidates)
    {
        if(mublock.light_count == MODEL_LIGHTS_MAX)
            break;

        mublock.packed_light_indices[mublock.light_count/4][mublock.light_count%4] = cand.idx;
        mublock.light_count++;
    }

    return mublock.light_count;
}

