#pragma once

#include <newbase/render/material.hpp>
#include <cstdint>

namespace nb
{
    class rshader;
}

namespace nb::render
{

/// Parameters for an internal IBL integration draw.
struct ibl_integration_params
{
    rtexture *source {nullptr};
    uint32_t face_index {0};
    uint32_t mip_level {0};
    uint32_t mip_count {1};
    uint32_t sample_count {128};
    float source_resolution {1.0f};
    uint32_t source_mip_count {1};
    sampler_config source_sampling {
        sampler_filter::LINEAR,
        sampler_filter::LINEAR,
        sampler_filter::LINEAR,
        sampler_addressing::REPEAT,
        sampler_addressing::CLAMP_TO_EDGE,
        sampler_addressing::CLAMP_TO_EDGE
    };
};

/// Material controller used by the renderer's environment preparation passes.
class ibl_integration_controller final : public material_controller
{
public:
    bool load_shaders();

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
    size_t get_num_texture_bindings(const rmaterial &mat, const pipeline_descriptor &desc,
                                    const mat_env_data *env_data) const override;
    void collect_texture_bindings(const rmaterial &mat, const pipeline_descriptor &desc,
                                  const mat_env_data *env_data,
                                  material_texture_binding *out_bindings) const override;

private:
    std::shared_ptr<rshader> m_fullscreen_vertex;
    std::shared_ptr<rshader> m_fragment[3];
};

}
