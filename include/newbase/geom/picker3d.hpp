#pragma once

#include <newbase/components/camera.hpp>
#include <newbase/components/layers.hpp>
#include <newbase/components/mesh.hpp>
#include <newbase/components/spatial.hpp>
#include <newbase/engine.hpp>
#include <newbase/layer.hpp>
#include <newbase/render/vertex.hpp>
#include <newbase/scene.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace nb::geom
{

/**
 * 3D picking implementation on the CPU side.
 * Casts a ray from the camera and hit-tests 3D mesh data and spatial entities.
 */
class picker_3d
{
public:
    static entt::entity pick(const render_layer &layer, float vp_x, float vp_y)
    {
        if (layer.viewport.w <= 0 || layer.viewport.h <= 0 ||
            !std::isfinite(vp_x) || !std::isfinite(vp_y) ||
            vp_x < 0.f || vp_y < 0.f ||
            vp_x >= static_cast<float>(layer.viewport.w) ||
            vp_y >= static_cast<float>(layer.viewport.h))
        {
            return entt::null;
        }

        auto *sc = engine::instance().find_scene(layer.scene_id);
        if (!sc)
            return entt::null;

        auto &reg = sc->registry();
        const auto *camera = reg.try_get<ccamera>(layer.camera);
        const auto *camera_spatial = reg.try_get<cspatial>(layer.camera);
        if (!camera || !camera_spatial)
            return entt::null;

        const float aspect = static_cast<float>(layer.viewport.w) /
                             static_cast<float>(layer.viewport.h);
        const glm::mat4 projection = glm::perspective(
            camera->cam3d.fov, aspect, camera->cam3d.clip_near, camera->cam3d.clip_far);
        const glm::mat4 view_projection = projection * glm::inverse(camera_spatial->world);
        const float determinant = glm::determinant(view_projection);
        if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-8f)
            return entt::null;

        const glm::mat4 inverse_view_projection = glm::inverse(view_projection);
        const float ndc_x = 2.f * vp_x / static_cast<float>(layer.viewport.w) - 1.f;
        const float ndc_y = 1.f - 2.f * vp_y / static_cast<float>(layer.viewport.h);

        glm::vec4 near_point = inverse_view_projection * glm::vec4{ndc_x, ndc_y, 0.f, 1.f};
        glm::vec4 far_point = inverse_view_projection * glm::vec4{ndc_x, ndc_y, 1.f, 1.f};
        if (!std::isfinite(near_point.w) || !std::isfinite(far_point.w) ||
            std::abs(near_point.w) < 1.0e-8f || std::abs(far_point.w) < 1.0e-8f)
        {
            return entt::null;
        }

        const glm::vec3 ray_origin = glm::vec3{near_point} / near_point.w;
        const glm::vec3 ray_end = glm::vec3{far_point} / far_point.w;
        const glm::vec3 ray_direction = glm::normalize(ray_end - ray_origin);
        if (!std::isfinite(ray_direction.x) || !std::isfinite(ray_direction.y) ||
            !std::isfinite(ray_direction.z))
        {
            return entt::null;
        }

        entt::entity best = entt::null;
        float best_distance = std::numeric_limits<float>::max();

        for (auto [id, mesh_component, spatial] : reg.view<const cmesh, const cspatial>().each())
        {
            const auto *layers = reg.try_get<clayers>(id);
            const uint32_t entity_mask = layers ? layers->mask : clayers::MASK_DEFAULT;
            if (!(entity_mask & layer.layer_mask) ||
                !mesh_component.mesh || !mesh_component.mesh->has_data())
            {
                continue;
            }

            const auto &mesh = *mesh_component.mesh;
            const auto &descriptor_table = render::vertex_type_descriptor::descriptor_table();
            const auto descriptor_it = descriptor_table.find(mesh.vertex_type());
            if (descriptor_it == descriptor_table.end())
                continue;

            const auto &descriptor = descriptor_it->second;
            const auto position_it = std::find_if(
                descriptor.attributes.begin(), descriptor.attributes.end(),
                [](const render::vertex_attribute_descriptor &attribute) {
                    return attribute.attr == render::vertex_attribute_type::POSITION &&
                           attribute.format == render::vertex_data_type::FLOAT3;
                });
            if (position_it == descriptor.attributes.end() || descriptor.size == 0)
                continue;

            const float world_determinant = glm::determinant(spatial.world);
            if (!std::isfinite(world_determinant) || std::abs(world_determinant) < 1.0e-8f)
                continue;

            const glm::mat4 inverse_world = glm::inverse(spatial.world);
            const glm::vec3 local_origin =
                glm::vec3{inverse_world * glm::vec4{ray_origin, 1.f}};
            const glm::vec3 local_direction =
                glm::vec3{inverse_world * glm::vec4{ray_direction, 0.f}};
            const auto *vertex_data = mesh.data();
            const size_t vertex_data_size = mesh.get_data_index_offset();
            if (vertex_data_size > mesh.data_size())
                continue;
            const uint32_t vertex_count =
                static_cast<uint32_t>(vertex_data_size / descriptor.size);
            const uint32_t *index_data = reinterpret_cast<const uint32_t *>(
                vertex_data + mesh.get_data_index_offset());
            const size_t index_count =
                (mesh.data_size() - mesh.get_data_index_offset()) / sizeof(uint32_t);

            for (const auto &submesh : mesh.submeshes())
            {
                const size_t index_end =
                    static_cast<size_t>(submesh.index_offset) + submesh.index_count;
                if (submesh.index_count < 3 || index_end > index_count)
                    continue;

                for (size_t index = submesh.index_offset;
                     index + 2 < index_end; index += 3)
                {
                    uint32_t vertex_indices[3] {};
                    bool valid_triangle = true;
                    for (size_t corner = 0; corner < 3; ++corner)
                    {
                        const uint64_t vertex_index =
                            static_cast<uint64_t>(submesh.vertex_offset) +
                            index_data[index + corner];
                        if (vertex_index >= vertex_count)
                        {
                            valid_triangle = false;
                            break;
                        }
                        vertex_indices[corner] = static_cast<uint32_t>(vertex_index);
                    }
                    if (!valid_triangle)
                        continue;

                    glm::vec3 positions[3];
                    for (size_t corner = 0; corner < 3; ++corner)
                    {
                        const size_t position_offset =
                            static_cast<size_t>(vertex_indices[corner]) * descriptor.size +
                            position_it->offset;
                        if (position_offset + sizeof(glm::vec3) > vertex_data_size)
                        {
                            valid_triangle = false;
                            break;
                        }
                        std::memcpy(&positions[corner], vertex_data + position_offset,
                                    sizeof(glm::vec3));
                    }
                    if (!valid_triangle)
                        continue;

                    const glm::vec3 edge1 = positions[1] - positions[0];
                    const glm::vec3 edge2 = positions[2] - positions[0];
                    const glm::vec3 p = glm::cross(local_direction, edge2);
                    const float determinant = glm::dot(edge1, p);
                    if (std::abs(determinant) < 1.0e-8f)
                        continue;

                    const float inverse_determinant = 1.f / determinant;
                    const glm::vec3 from_vertex = local_origin - positions[0];
                    const float u = glm::dot(from_vertex, p) * inverse_determinant;
                    if (u < 0.f || u > 1.f)
                        continue;

                    const glm::vec3 q = glm::cross(from_vertex, edge1);
                    const float v = glm::dot(local_direction, q) * inverse_determinant;
                    if (v < 0.f || u + v > 1.f)
                        continue;

                    const float distance = glm::dot(edge2, q) * inverse_determinant;
                    if (distance >= 0.f && distance < best_distance)
                    {
                        best = id;
                        best_distance = distance;
                    }
                }
            }
        }

        return best;
    }
};

} // namespace nb::geom
