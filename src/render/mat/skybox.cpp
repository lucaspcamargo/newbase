#include <newbase/render/mat/skybox.hpp>
#include <newbase/render/types.hpp>
#include <newbase/res/material.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/res/shader.hpp>
#include <newbase/res/texture.hpp>
#include <newbase/log.hpp>
#include <entt/core/hashed_string.hpp>

using namespace nb;
using namespace nb::render;
using entt::operator""_hs;

struct sb_uniforms
{
    float sample_level {0.0f};
    float _pad[3] {};
};

skybox_mat_controller::skybox_mat_controller()
{
    m_vertex = rman().load_sync<rshader>("_nb_core/slang/skybox.vert.slang"_hs);
    m_fragment = rman().load_sync<rshader>("_nb_core/slang/skybox.frag.slang"_hs);
    if(!m_vertex || !m_fragment)
        log::error("[skybox] failed to load shaders");
}

void skybox_mat_controller::make_pipeline_desc(const rmaterial &mat, const mat_env_data *,
                                                pass_type, vertex_type vt,
                                                pipeline_descriptor &dst_desc) const
{
    dst_desc.mat_type = material_type::SKYBOX;
    dst_desc.params = mat.pipeline_params;
    dst_desc.vertex_fmt = vt;
    dst_desc.shader_variant = 0;
}

std::shared_ptr<rshader>
skybox_mat_controller::get_vertex_program(const pipeline_descriptor &) const
{
    return m_vertex;
}

std::shared_ptr<rshader>
skybox_mat_controller::get_fragment_program(const pipeline_descriptor &) const
{
    return m_fragment;
}

size_t skybox_mat_controller::get_vertex_ubo_size(const pipeline_descriptor &,
                                                   const mat_env_data *) const
{
    return 0;
}

size_t skybox_mat_controller::get_fragment_ubo_size(const pipeline_descriptor &,
                                                     const mat_env_data *) const
{
    return sizeof(sb_uniforms);
}

void skybox_mat_controller::pack_vertex_uniforms(const rmaterial &,
                                                 const pipeline_descriptor &,
                                                 const mat_env_data *,
                                                 uint8_t *, size_t) const
{
}

void skybox_mat_controller::pack_fragment_uniforms(const rmaterial &mat,
                                                   const pipeline_descriptor &,
                                                   const mat_env_data *,
                                                   uint8_t *dest, size_t dest_sz) const
{
    const auto params = mat.mat_params.try_cast<const skybox_mat_params>();
    if(!params)
        return;
    sb_uniforms ret {};
    ret.sample_level = 3.0f;//params->sample_level;
    *((sb_uniforms*) dest) = ret;
}

size_t skybox_mat_controller::get_num_texture_bindings(const rmaterial &mat,
                                                        const pipeline_descriptor &,
                                                        const mat_env_data *) const
{
    const auto params = mat.mat_params.try_cast<const skybox_mat_params>();
    return params && params->cubemap ? 1 : 0;
}

void skybox_mat_controller::collect_texture_bindings(const rmaterial &mat,
                                                      const pipeline_descriptor &,
                                                      const mat_env_data *,
                                                      material_texture_binding *out_bindings) const
{
    const auto params = mat.mat_params.try_cast<const skybox_mat_params>();
    if(!params || !params->cubemap || !out_bindings)
        return;

    out_bindings[0] = material_texture_binding {
        .texture = params->cubemap.get(),
        .slot = 0,
        .sampler = params->sampling,
        .vertex = false
    };
}
