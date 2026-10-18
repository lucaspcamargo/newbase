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
        .spatial = spatial,
        .world_aabb = la,
        .pos_center = pos,
        .cam_dist_sq = 0.0f, // calculated later if needed
        .positional = is_positional,
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

void light_culler::for_each_shadow_map(glm::vec3 cam_pos, shadow_map_callback_t cb)
{
    // calculate light distances from camera, and sort
    for(auto &le: m_culled_lights)
    {
        le.cam_dist_sq = glm::distance2(cam_pos, le.pos_center);
    }
    std::sort(m_culled_lights.begin(), m_culled_lights.end(), +[](const light_entry &a,
                                                                  const light_entry &b){
        return a.cam_dist_sq < b.cam_dist_sq;
    });

    for(auto &le: m_culled_lights)
    {
        auto &light = *le.light;
        if(!light.casts_shadows)
            continue;
        switch(light.type)
        {
            case light_type::SPOT:
            {
                // 1. Construct the Light View Matrix
                glm::vec3 light_pos = glm::vec3{0.0f};
                glm::vec3 light_dir = glm::vec3{0.f, -1.f, 0.f};
                if(le.spatial)
                {
                    const auto transform = le.spatial->world;
                    light_pos = glm::vec3{transform[3]}; // last col
                    light_dir = glm::normalize(glm::mat3{transform} * glm::vec3{0.f, -1.f, 0.f});
                }
                const glm::vec3 light_target = light_pos + light_dir;

                glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
                if (std::abs(glm::dot(glm::normalize(light_dir), up)) > 0.999f) {
                    // if the light is straight up/down, use z as fallback up
                    up = glm::vec3(0.0f, 0.0f, 1.0f);
                }

                const glm::mat4 light_view = glm::lookAt(light_pos, light_target, up);

                float fov = light.outer_cone_angle * 2.0f;
                float aspect = 1.0f;
                float near_plane = 0.1f;
                float far_plane = light.range;

                glm::mat4 light_proj = glm::perspective(fov, aspect, near_plane, far_plane);

                auto lvp = light_proj * light_view;

                cb(&light, 1, &lvp);
            }

            default:
                break; // only spot for now
        }
    }
}

