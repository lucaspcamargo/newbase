#pragma once

#include "entt/core/fwd.hpp"
#include <cstdint>
#include <newbase/res/resource.hpp>
#include <newbase/render/vertex.hpp>

// Our engine-native mesh resource
// Currently madde to load and extract data from gltf resources,
// interleaving and converting the data to native mesh resourecs in the process
// A mesh has a single combined geometry and index buffer, but it defines submeshes
// The submeshes have a material resource reference, and define ranges in the
// GPU buffer for rendering.
// Indices are always uin32_t for avoiding complex submesh splitting.

namespace nb { class rgltf; }
struct tg3_model;
struct tg3_mesh;

namespace nb
{

class rmesh : public resource
{
public:
    explicit rmesh(entt::id_type id = 0);
    ~rmesh() override;

    const std::vector<dependency_t>* dependencies() const override { return &m_deps; }

    static void _init_rtti();

    struct submesh
    {
        int32_t  material_idx;
        uint32_t vertex_offset {0};
        uint32_t index_offset {0};
        uint32_t index_count {0};
    };

    bool has_data() const { return !m_data.empty(); }
    const uint8_t * data() const { return m_data.data(); }
    size_t data_size() const { return m_data.size(); }
    uint32_t get_data_index_offset() const { return m_data_index_offset; }
    const std::vector<submesh>& submeshes() const { return m_submeshes; }
    render::vertex_type vertex_type() const { return m_vtx_type; }
    const std::vector<entt::id_type>& material_slots() const { return m_mat_slots; }

    entt::entity tmp_add_to_default_scene();  // TEST

    bool uploaded {false};
    void *rptr {nullptr};
    void (*on_delete)(rmesh &t, void *uptr) {nullptr};
    void *on_delete_uptr {nullptr};

protected:
    bool do_preload() override;

    bool do_load() override;

private:
    bool _load_gltf_model_mesh(const tg3_model *model, const tg3_mesh *mesh, const rgltf& parent_gltf);
    int32_t _allocate_material_slot(int32_t moel_mat_index, const rgltf& parent_gltf);

    render::vertex_type m_vtx_type{render::vertex_type::INVALID};
    std::vector<uint8_t> m_data {};
    uint32_t m_data_index_offset {0};
    std::vector<entt::id_type> m_mat_slots {}; // maps local submesh material indices to material res ids
    int m_empty_mat_slot = -1;
    std::vector<submesh> m_submeshes {};
    std::vector<dependency_t> m_deps {};
};

}
