#pragma once

#include <newbase/render/material.hpp>

namespace nb::render
{

struct matcap_mat_params
{
    std::shared_ptr<rtexture> matcap_texture;
    sampler_config matcap_sampling {
        sampler_filter::LINEAR,
        sampler_filter::LINEAR,
        sampler_filter::LINEAR,
        sampler_addressing::CLAMP_TO_EDGE,
        sampler_addressing::CLAMP_TO_EDGE,
        sampler_addressing::CLAMP_TO_EDGE
    };
    glm::vec4 base_color_factor {1.0f};
    float matcap_intensity {1.0f};
    std::shared_ptr<rtexture> normal_texture;
    sampler_config normal_sampling {};
    float normal_scaling {1.0f};
    std::shared_ptr<rtexture> ao_texture;
    sampler_config ao_sampling {};
    float occlusion_strength {1.0f};
};

class matcap_mat_controller final : public material_controller
{
public:
    matcap_mat_controller();

    void make_pipeline_desc(const rmaterial &mat, const mat_env_data *env_data,
                            pass_type pt, vertex_type vt,
                            pipeline_descriptor &dst_desc) const override;
    std::shared_ptr<rshader> get_vertex_program(const pipeline_descriptor &desc) const override;
    std::shared_ptr<rshader> get_fragment_program(const pipeline_descriptor &desc) const override;
    size_t get_vertex_ubo_size(const pipeline_descriptor &desc,
                               const mat_env_data *env_data) const override;
    size_t get_fragment_ubo_size(const pipeline_descriptor &desc,
                                 const mat_env_data *env_data) const override;
    void pack_vertex_uniforms(const rmaterial &mat, const pipeline_descriptor &desc,
                              const mat_env_data *env_data,
                              uint8_t *out_buffer, size_t out_len) const override;
    void pack_fragment_uniforms(const rmaterial &mat, const pipeline_descriptor &desc,
                                const mat_env_data *env_data,
                                uint8_t *out_buffer, size_t out_len) const override;
    size_t get_num_texture_bindings(const rmaterial &mat,
                                    const pipeline_descriptor &desc,
                                    const mat_env_data *env_data) const override;
    void collect_texture_bindings(const rmaterial &mat,
                                  const pipeline_descriptor &desc,
                                  const mat_env_data *env_data,
                                  material_texture_binding *out_bindings) const override;

private:
    std::shared_ptr<rshader> m_vertex;
    std::shared_ptr<rshader> m_fragment;
    std::shared_ptr<rtexture> m_fallback_normal;
    std::shared_ptr<rtexture> m_fallback_ao;
};

}