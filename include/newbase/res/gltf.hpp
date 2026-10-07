#pragma once

#include "entt/core/fwd.hpp"
#include <newbase/res/resource.hpp>

struct tg3_model;

namespace nb
{

class rgltf : public resource
{
public:
    explicit rgltf(entt::id_type id = 0);
    ~rgltf() override;

    const std::vector<dependency_t>* dependencies() const override { return &m_deps; }

    bool has_model() { return m_model.get(); }
    const tg3_model * get_model() { return m_model.get(); }

    const std::vector<entt::id_type>& get_mesh_res_ids() const { return m_mesh_res_ids; }
    const std::vector<entt::id_type>& get_material_res_ids() const { return m_mat_res_ids; }
    const std::vector<entt::id_type>& get_texture_res_ids() const { return m_tex_res_ids; }
    const std::vector<entt::id_type>& get_scene_res_ids() const { return m_scn_res_ids; }

    bool get_subresource_data(entt::id_type sub, std::vector<uint8_t>&) override;

    static void _init_rtti();

protected:
    bool do_preload() override;
    bool do_load() override;

private:
    std::vector<char> m_data;
    std::vector<dependency_t> m_deps;
    std::unique_ptr<tg3_model> m_model {nullptr};

    std::vector<entt::id_type> m_mesh_res_ids;
    std::vector<entt::id_type> m_mat_res_ids;
    std::vector<entt::id_type> m_tex_res_ids;
    std::vector<entt::id_type> m_scn_res_ids;
};

}
