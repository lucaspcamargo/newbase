#include <newbase/render/mat/ibl_integration.hpp>
#include <newbase/res/material.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/res/shader.hpp>
#include <newbase/log.hpp>
#include <entt/core/hashed_string.hpp>
#include <utility>

using namespace nb;
using namespace nb::render;
using entt::operator""_hs;

bool ibl_integration_controller::load_shaders()
{
    if(m_fullscreen_vertex && m_fragment[0] && m_fragment[1] && m_fragment[2])
        return true;

    auto vertex = rman().load_sync<rshader>(
        entt::hashed_string{"_nb_core/slang/ibl_fullscreen.vert.slang"}.value());
    auto equirectangular = rman().load_sync<rshader>(
        entt::hashed_string{"_nb_core/slang/ibl_equirect_to_cubemap.frag.slang"}.value());
    auto irradiance = rman().load_sync<rshader>(
        entt::hashed_string{"_nb_core/slang/ibl_diffuse_irradiance.frag.slang"}.value());
    auto prefilter = rman().load_sync<rshader>(
        entt::hashed_string{"_nb_core/slang/ibl_specular_prefilter.frag.slang"}.value());
    if(!vertex || !equirectangular || !irradiance || !prefilter)
    {
        log::error("[ibl] failed to load environment integration shaders");
        return false;
    }
    m_fullscreen_vertex = std::move(vertex);
    m_fragment[0] = std::move(equirectangular);
    m_fragment[1] = std::move(irradiance);
    m_fragment[2] = std::move(prefilter);
    return true;
}

void ibl_integration_controller::make_pipeline_desc(const rmaterial &mat, const mat_env_data *,
        pass_type pt, vertex_type vt, pipeline_descriptor &dst_desc) const
{
    dst_desc.mat_type = material_type::IBL_INTEGRATION;
    dst_desc.params = mat.pipeline_params;
    dst_desc.vertex_fmt = vt;
    switch(pt)
    {
        case pass_type::EQUIRECTANGULAR_TO_CUBEMAP:
            dst_desc.shader_variant = 0;
            break;
        case pass_type::DIFFUSE_IRRADIANCE:
            dst_desc.shader_variant = 1;
            break;
        case pass_type::SPECULAR_PREFILTER:
            dst_desc.shader_variant = 2;
            break;
        default:
            dst_desc.shader_variant = UINT16_MAX;
            break;
    }
}

std::shared_ptr<rshader> ibl_integration_controller::get_vertex_program(const pipeline_descriptor &desc) const
{
    return desc.shader_variant < 3 ? m_fullscreen_vertex : nullptr;
}

std::shared_ptr<rshader> ibl_integration_controller::get_fragment_program(const pipeline_descriptor &desc) const
{
    return desc.shader_variant < 3 ? m_fragment[desc.shader_variant] : nullptr;
}

size_t ibl_integration_controller::get_vertex_ubo_size(const pipeline_descriptor &,
        const mat_env_data *) const
{
    return 0;
}

struct alignas(16) integration_fragment_uniforms
{
    uint32_t face_index;
    uint32_t mip_level;
    uint32_t mip_count;
    uint32_t sample_count;
    float source_resolution;
    uint32_t source_mip_count;
    float padding[2];
};

size_t ibl_integration_controller::get_fragment_ubo_size(const pipeline_descriptor &desc,
        const mat_env_data *) const
{
    return desc.shader_variant < 3 ? sizeof(integration_fragment_uniforms) : 0;
}

void ibl_integration_controller::pack_vertex_uniforms(const rmaterial &, const pipeline_descriptor &,
        const mat_env_data *, uint8_t *, size_t) const
{
}

void ibl_integration_controller::pack_fragment_uniforms(const rmaterial &mat,
        const pipeline_descriptor &desc, const mat_env_data *,
        uint8_t *out_buffer, size_t out_len) const
{
    if(desc.shader_variant >= 3 || out_len != sizeof(integration_fragment_uniforms) || !out_buffer)
        return;
    const auto params = mat.mat_params.try_cast<const ibl_integration_params>();
    if(!params)
        return;
    auto &uniforms = *reinterpret_cast<integration_fragment_uniforms*>(out_buffer);
    uniforms.face_index = params->face_index;
    uniforms.mip_level = params->mip_level;
    uniforms.mip_count = params->mip_count;
    uniforms.sample_count = params->sample_count;
    uniforms.source_resolution = params->source_resolution;
    uniforms.source_mip_count = params->source_mip_count;
    uniforms.padding[0] = uniforms.padding[1] = 0.0f;
}

size_t ibl_integration_controller::get_num_texture_bindings(const rmaterial &mat,
        const pipeline_descriptor &desc, const mat_env_data *) const
{
    const auto params = mat.mat_params.try_cast<const ibl_integration_params>();
    return params && params->source && desc.shader_variant < 3 ? 1 : 0;
}

void ibl_integration_controller::collect_texture_bindings(const rmaterial &mat,
        const pipeline_descriptor &desc, const mat_env_data *,
        material_texture_binding *out_bindings) const
{
    const auto params = mat.mat_params.try_cast<const ibl_integration_params>();
    if(!params || !params->source || !out_bindings || desc.shader_variant >= 3)
        return;
    out_bindings[0] = material_texture_binding {
        .texture = params->source,
        .slot = 0,
        .sampler = params->source_sampling,
        .vertex = false
    };
}
