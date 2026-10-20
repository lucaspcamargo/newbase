#pragma once
#include <newbase/render/material.hpp>
#include <newbase/render/pass.hpp>
#include <newbase/render/types.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/res/shader.hpp>
#include <entt/entt.hpp>

using entt::operator""_hs;

namespace nb::render
{


/// a material that simply writes depth
/// the fragment shader does nothing

class depth_material : public material_controller
{
public:

    depth_material()
    {
        m_vert = rman().load_sync<rshader>("_nb_core/slang/depth.vert.slang"_hs);
        m_frag = rman().load_sync<rshader>("_nb_core/slang/depth.frag.slang"_hs);
    }

    void make_pipeline_desc(const rmaterial &mat, const mat_env_data *env_data,
                            pass_type pt, vertex_type vt,
                            pipeline_descriptor &dst_desc) const override
    {
        dst_desc.mat_type = material_type::DEPTH;
        dst_desc.params.depth_test = dst_desc.params.depth_write = true;
        dst_desc.params.cull = pt == pass_type::DEPTH_ONLY_SHADOW_MAP? cull_mode::FRONT : cull_mode::NONE;
        dst_desc.shader_variant = 0;
        dst_desc.vertex_fmt = vt;
    }

    std::shared_ptr<rshader>
    get_vertex_program(const pipeline_descriptor &desc) const override
    {
        return m_vert;
    }

    std::shared_ptr<rshader>
    get_fragment_program(const pipeline_descriptor &desc) const override
    {
        return m_frag;
    }

    size_t get_vertex_ubo_size(const pipeline_descriptor &desc,
                                const mat_env_data *env_data) const override
    {
        return 0;
    }

    size_t get_fragment_ubo_size(const pipeline_descriptor &desc,
                                const mat_env_data *env_data) const override
    {
        return 0;
    }

    void pack_vertex_uniforms(const rmaterial &mat,
                                const pipeline_descriptor &desc,
                                const mat_env_data *env_data, uint8_t *out_buffer,
                                size_t out_len) const override
    {
    }

    void pack_fragment_uniforms(const rmaterial &mat,
                                const pipeline_descriptor &desc,
                                const mat_env_data *env_data, uint8_t *out_buffer,
                                size_t out_len) const override
    {
    }

    size_t get_num_texture_bindings(const rmaterial &mat,
                                    const pipeline_descriptor &dest,
                                    const mat_env_data *env_data) const override
    {
        return 0;
    }

    void collect_texture_bindings(
        const rmaterial &mat, const pipeline_descriptor &desc,
        const mat_env_data *env_data,
        material_texture_binding *out_bindings_buf) const override
    {} // no textures

private:
    std::shared_ptr<rshader> m_vert {nullptr}, m_frag {nullptr};
};

}
