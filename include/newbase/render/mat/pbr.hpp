#include "newbase/render/pipeline.hpp"
#include "newbase/render/types.hpp"
#include <newbase/render/material.hpp>
#include <newbase/utility/glm.hpp>


namespace nb::render
{

struct pbr_mat_params
{
    std::shared_ptr<rtexture> base_color_tex {nullptr};
    render::sampler_config base_color_sampling {};
    glm::vec4 base_color_factor {1.0f};
    std::shared_ptr<rtexture> normal_tex {nullptr};
    render::sampler_config normal_sampling {};
    float normal_scaling {1.0f};
    std::shared_ptr<rtexture> metal_rough_tex {nullptr};
    render::sampler_config metal_rough_sampling {};
    float  metallic_factor {1.0f};
    float  roughness_factor {1.0f};
    std::shared_ptr<rtexture> ao_tex {nullptr};
    render::sampler_config ao_sampling {};
    float occlusion_strength {1.0f};
    std::shared_ptr<rtexture> emissive_color_tex {nullptr};
    render::sampler_config emissive_color_sampling {};
    glm::vec4 emissive_color_factor {0.0f};
};

class pbr_mat_controller : public material_controller
{
public:
    pbr_mat_controller();
    ~pbr_mat_controller() override;

    /// prepares a pipeline for a pbr material (except for target format setup)
    void make_pipeline_desc(const rmaterial& mat, const mat_env_data *env_data,
                            pass_type pt, vertex_type vt,
                            pipeline_descriptor &dst_desc) const override;

    /// returns the vertex program associated with this material type
    std::shared_ptr<rshader> get_vertex_program(const pipeline_descriptor& pd) const override;

    /// returns the fragment program associated with this material type
    std::shared_ptr<rshader> get_fragment_program(const pipeline_descriptor& pd) const override;

    /// returns the raw structure size for the vertex shader ubo
    size_t get_vertex_ubo_size(const pipeline_descriptor& pd,
                               const mat_env_data *env_data) const override;

    /// returns the raw structure size for the fragment shader ubo
    size_t get_fragment_ubo_size(const pipeline_descriptor& pd,
                                 const mat_env_data *env_data) const override;

    /// generates the fragment uniform buffer for this material
    void pack_vertex_uniforms( const rmaterial& mat, const pipeline_descriptor& pd,
                               const mat_env_data *env_data, uint8_t* out_buffer,
                               size_t out_len) const override;

    /// generates the fragment uniform buffer for this material
    void pack_fragment_uniforms(const rmaterial& mat, const pipeline_descriptor& pd,
                                const mat_env_data *env_data, uint8_t* out_buffer,
                                        size_t out_len) const override;


    size_t get_num_texture_bindings(const rmaterial& mat, const pipeline_descriptor &dest,
                                    const mat_env_data *env_data) const override;

    /// returns all the textures required for the shader
    void collect_texture_bindings( const rmaterial& mat, const pipeline_descriptor& pd,
                                   const mat_env_data *env_data,
                                   material_texture_binding *out) const override;

    static void _init_rtti();

private:
    std::shared_ptr<rshader>  m_frag;
    std::shared_ptr<rshader>  m_frag_ibl;
    std::shared_ptr<rshader>  m_vert;
    std::shared_ptr<rtexture> m_fallback_color;
    std::shared_ptr<rtexture> m_fallback_normal;
    std::shared_ptr<rtexture> m_fallback_metal;
    std::shared_ptr<rtexture> m_fallback_ao;
    std::shared_ptr<rtexture> m_fallback_emissive;
    std::shared_ptr<rtexture> m_brdf_lut;
};

}
