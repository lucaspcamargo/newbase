#include <newbase/render/mat/matcap.hpp>
#include <newbase/render/types.hpp>
#include <newbase/res/material.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/res/shader.hpp>
#include <newbase/res/texture.hpp>
#include <newbase/log.hpp>
#include <entt/core/hashed_string.hpp>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <cassert>

using namespace nb;
using namespace nb::render;
using entt::operator""_hs;

namespace
{

struct alignas(16) matcap_fragment_uniforms
{
    glm::vec4 base_color_factor;
    float matcap_intensity;
    float normal_scaling;
    float occlusion_strength;
    float padding;
};

std::shared_ptr<rtexture> make_fallback_texture(uint8_t r, uint8_t g,
                                               uint8_t b, uint8_t a)
{
    auto *surface = SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32);
    if(!surface)
    {
        log::error("[matcap] failed to create fallback texture surface: %s",
                   SDL_GetError());
        return nullptr;
    }
    auto *pixels = static_cast<uint8_t*>(surface->pixels);
    pixels[0] = r;
    pixels[1] = g;
    pixels[2] = b;
    pixels[3] = a;

    auto texture = std::make_shared<rtexture>();
    texture->load_from(surface);
    return texture;
}

}

matcap_mat_controller::matcap_mat_controller()
{
    m_vertex = rman().load_sync<rshader>("_nb_core/slang/matcap.vert.slang"_hs);
    m_fragment = rman().load_sync<rshader>("_nb_core/slang/matcap.frag.slang"_hs);
    m_fallback_normal = make_fallback_texture(128, 128, 255, 255);
    m_fallback_ao = make_fallback_texture(255, 255, 255, 255);
    if(!m_vertex || !m_fragment)
        log::error("[matcap] failed to load shaders");
    if(!m_fallback_normal || !m_fallback_ao)
        log::error("[matcap] failed to create fallback textures");
}

void matcap_mat_controller::make_pipeline_desc(const rmaterial &mat, const mat_env_data *,
                                                pass_type, vertex_type vt,
                                                pipeline_descriptor &dst_desc) const
{
    dst_desc.mat_type = material_type::MATCAP;
    dst_desc.params = mat.pipeline_params;
    dst_desc.vertex_fmt = vt;
    dst_desc.shader_variant = 0;
}

std::shared_ptr<rshader>
matcap_mat_controller::get_vertex_program(const pipeline_descriptor &) const
{
    return m_vertex;
}

std::shared_ptr<rshader>
matcap_mat_controller::get_fragment_program(const pipeline_descriptor &) const
{
    return m_fragment;
}

size_t matcap_mat_controller::get_vertex_ubo_size(const pipeline_descriptor &,
                                                   const mat_env_data *) const
{
    return 0;
}

size_t matcap_mat_controller::get_fragment_ubo_size(const pipeline_descriptor &,
                                                     const mat_env_data *) const
{
    return sizeof(matcap_fragment_uniforms);
}

void matcap_mat_controller::pack_vertex_uniforms(const rmaterial &,
                                                 const pipeline_descriptor &,
                                                 const mat_env_data *,
                                                 uint8_t *, size_t) const
{
}

void matcap_mat_controller::pack_fragment_uniforms(const rmaterial &mat,
                                                   const pipeline_descriptor &,
                                                   const mat_env_data *,
                                                   uint8_t *out_buffer,
                                                   size_t out_len) const
{
    assert(out_len == sizeof(matcap_fragment_uniforms));
    const auto params = mat.mat_params.try_cast<const matcap_mat_params>();
    if(!params || !out_buffer || out_len != sizeof(matcap_fragment_uniforms))
    {
        log::warn("[matcap] invalid material parameters");
        return;
    }

    auto &uniforms = *reinterpret_cast<matcap_fragment_uniforms*>(out_buffer);
    uniforms.base_color_factor = params->base_color_factor;
    uniforms.matcap_intensity = params->matcap_intensity;
    uniforms.normal_scaling = params->normal_scaling;
    uniforms.occlusion_strength = params->occlusion_strength;
    uniforms.padding = 0.0f;
}

size_t matcap_mat_controller::get_num_texture_bindings(const rmaterial &mat,
                                                        const pipeline_descriptor &,
                                                        const mat_env_data *) const
{
    const auto params = mat.mat_params.try_cast<const matcap_mat_params>();
    return params && params->matcap_texture && m_fallback_normal && m_fallback_ao ? 3 : 0;
}

void matcap_mat_controller::collect_texture_bindings(const rmaterial &mat,
                                                      const pipeline_descriptor &,
                                                      const mat_env_data *,
                                                      material_texture_binding *out_bindings) const
{
    const auto params = mat.mat_params.try_cast<const matcap_mat_params>();
    if(!params || !params->matcap_texture || !out_bindings ||
       !m_fallback_normal || !m_fallback_ao)
        return;

    out_bindings[0] = material_texture_binding {
        .texture = params->matcap_texture.get(),
        .slot = 0,
        .sampler = params->matcap_sampling,
        .vertex = false
    };
    out_bindings[1] = material_texture_binding {
        .texture = (params->normal_texture ? params->normal_texture : m_fallback_normal).get(),
        .slot = 1,
        .sampler = params->normal_sampling,
        .vertex = false
    };
    out_bindings[2] = material_texture_binding {
        .texture = (params->ao_texture ? params->ao_texture : m_fallback_ao).get(),
        .slot = 2,
        .sampler = params->ao_sampling,
        .vertex = false
    };
    params->matcap_texture->srgb_conversion = true;
    out_bindings[1].texture->srgb_conversion = false;
    out_bindings[2].texture->srgb_conversion = false;
}