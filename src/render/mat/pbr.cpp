#include <newbase/render/mat/pbr.hpp>
#include <newbase/res/shader.hpp>
#include <newbase/res/texture.hpp>
#include <newbase/res/material.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/log.hpp>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include "entt/core/hashed_string.hpp"
#include "entt/meta/policy.hpp"
#include "newbase/render/material.hpp"
#include "newbase/render/types.hpp"
#include <entt/entt.hpp>
#include <memory>


using namespace nb;
using namespace nb::render;
using entt::operator""_hs;


struct alignas(16) pbr_frag_uniforms
{
    glm::vec4 base_color_factor;
    glm::vec3 emissive_factor;
    float     metallic_factor;
    float     roughness_factor;
    float     normal_scaling;
    float     occlusion_strength;
    float _pad;
};

struct alignas(16) pbr_ibl_frag_uniforms
{
    glm::vec4 base_color_factor;
    glm::vec3 emissive_factor;
    float     metallic_factor;
    float     roughness_factor;
    float     normal_scaling;
    float     occlusion_strength;
    float     _pad;
    float     prefiltered_max_lod;
    float     padding[3];
};


static std::shared_ptr<rtexture> _make_fallback_tex(uint8_t r, uint8_t g, uint8_t b, uint8_t a);


pbr_mat_controller::pbr_mat_controller()
{
    log::info("[pbr] constructing");
    m_vert = rman().load_sync<rshader>("_nb_core/slang/pbr.vert.slang"_hs);
    m_frag = rman().load_sync<rshader>("_nb_core/slang/pbr.frag.slang"_hs);
    m_frag_ibl = rman().load_sync<rshader>("_nb_core/slang/pbr_ibl.frag.slang"_hs);

    m_fallback_color    = _make_fallback_tex(255, 255, 255, 255); // factor control
    m_fallback_normal   = _make_fallback_tex(128, 128, 255, 255);
    m_fallback_metal    = _make_fallback_tex(255, 255, 255, 255); // factor control
    m_fallback_ao       = _make_fallback_tex(255, 255, 255, 255); // no ao
    m_fallback_emissive = _make_fallback_tex(255, 255, 255, 255); // factor control
    m_brdf_lut = rman().load_sync<rtexture>("_nb_core/tex/brdf_lut.ktx2"_hs);
    if(!m_frag_ibl)
        log::error("[pbr] failed to load IBL fragment shader");
    if(!m_brdf_lut)
        log::error("[pbr] failed to load BRDF integration LUT");
}

pbr_mat_controller::~pbr_mat_controller()
{
    log::info("[pbr] destroying");
    m_vert.reset();
    m_frag.reset();
    m_frag_ibl.reset();

    m_fallback_color.reset();
    m_fallback_normal.reset();
    m_fallback_metal.reset();
    m_fallback_ao.reset();
    m_fallback_emissive.reset();
    m_brdf_lut.reset();
}


void pbr_mat_controller::make_pipeline_desc(const rmaterial& mat, const mat_env_data *env_data,
                        pass_type pt, vertex_type vt, pipeline_descriptor &dst_desc) const
{
    dst_desc.mat_type = material_type::STANDARD_PBR;
    dst_desc.params = mat.pipeline_params;
    dst_desc.shader_variant = env_data && env_data->ibl_enabled &&
        env_data->irradiance_cubemap && env_data->prefiltered_cubemap &&
        m_brdf_lut && m_frag_ibl ? 1 : 0;
    dst_desc.vertex_fmt = vt;
}

std::shared_ptr<rshader>
pbr_mat_controller::get_vertex_program(const pipeline_descriptor &dst_desc) const
{
    return m_vert;
}

std::shared_ptr<rshader>
pbr_mat_controller::get_fragment_program(const pipeline_descriptor &dst_desc) const
{
    return dst_desc.shader_variant == 1 ? m_frag_ibl : m_frag;
}

size_t pbr_mat_controller::get_vertex_ubo_size(const pipeline_descriptor &dst_desc,
                                               const mat_env_data *) const
{
    return 0;
}

size_t pbr_mat_controller::get_fragment_ubo_size(const pipeline_descriptor &dst_desc,
                                                 const mat_env_data *) const
{
    return dst_desc.shader_variant == 1 ? sizeof(pbr_ibl_frag_uniforms)
                                       : sizeof(pbr_frag_uniforms);
}

void pbr_mat_controller::pack_vertex_uniforms(const rmaterial &mat,
                                              const pipeline_descriptor &dst_desc,
                                              const mat_env_data *,
                                              uint8_t *out_buffer,
                                              size_t out_len) const
{
}

void pbr_mat_controller::pack_fragment_uniforms(const rmaterial &mat,
                                                const pipeline_descriptor &dst_desc,
                                                const mat_env_data *env_data,
                                                uint8_t *out_buffer,
                                                size_t out_len) const
{
    const bool ibl_enabled = dst_desc.shader_variant == 1;
    assert(out_len == (ibl_enabled ? sizeof(pbr_ibl_frag_uniforms) : sizeof(pbr_frag_uniforms)));

    const auto param_data = mat.mat_params.try_cast<const pbr_mat_params>();
    if(!param_data)
    {
        log::warn("[pbr] collect_texture_bindings: wrong parameter block type!");
        return;
    }

    // get params from mat
    auto pack_common = [&](auto &dest) {
        dest.base_color_factor = param_data->base_color_factor;
        dest.emissive_factor = param_data->emissive_color_factor;
        dest.metallic_factor = param_data->metallic_factor;
        dest.roughness_factor = param_data->roughness_factor;
        dest.normal_scaling = param_data->normal_scaling;
        dest.occlusion_strength = param_data->occlusion_strength;
        dest._pad = 0.0f;
    };
    if(ibl_enabled)
    {
        auto &dest = *reinterpret_cast<pbr_ibl_frag_uniforms*>(out_buffer);
        pack_common(dest);
        dest.prefiltered_max_lod = env_data && env_data->prefiltered_cubemap &&
            !env_data->prefiltered_cubemap->mipmaps.empty()
                ? static_cast<float>(env_data->prefiltered_cubemap->mipmaps.size() - 1) : 0.0f;
        dest.padding[0] = dest.padding[1] = dest.padding[2] = 0.0f;
    }
    else
    {
        auto &dest = *reinterpret_cast<pbr_frag_uniforms*>(out_buffer);
        pack_common(dest);
    }
}


size_t pbr_mat_controller::get_num_texture_bindings(const rmaterial& mat,
        const pipeline_descriptor &dest, const mat_env_data *env_data) const
{
    return 5 + (dest.shader_variant == 1 ? 3 : 0);
}

void pbr_mat_controller::collect_texture_bindings(const rmaterial &mat,
                                                  const pipeline_descriptor &dst_desc,
                                                  const mat_env_data *env_data,
                                                  material_texture_binding *out_bindings) const
{
    const auto param_data = mat.mat_params.try_cast<const pbr_mat_params>();
    if(!param_data)
    {
        log::warn("[pbr] collect_texture_bindings: wrong parameter block type!");
        return;
    }

    // color
    out_bindings[0] = material_texture_binding{
        .texture = (param_data->base_color_tex? param_data->base_color_tex : m_fallback_color).get(),
        .slot = 0,
        .sampler = param_data->base_color_sampling,
        .vertex = false
    };

    // normal
    out_bindings[1] = material_texture_binding{
        .texture = (param_data->normal_tex? param_data->normal_tex : m_fallback_normal).get(),
        .slot = 1,
        .sampler = param_data->normal_sampling,
        .vertex = false
    };

    // metal-rough
    out_bindings[2] = material_texture_binding{
        .texture = (param_data->metal_rough_tex? param_data->metal_rough_tex : m_fallback_metal).get(),
        .slot = 2,
        .sampler = param_data->metal_rough_sampling,
        .vertex = false
    };

    // AO
    out_bindings[3] = material_texture_binding{
        .texture = (param_data->ao_tex? param_data->ao_tex : m_fallback_ao).get(),
        .slot = 3,
        .sampler = param_data->ao_sampling,
        .vertex = false
    };

    // emissive
    out_bindings[4] = material_texture_binding{
        .texture = (param_data->emissive_color_tex? param_data->emissive_color_tex : m_fallback_emissive).get(),
        .slot = 4,
        .sampler = param_data->emissive_color_sampling,
        .vertex = false
    };
    if(dst_desc.shader_variant == 1 && env_data)
    {
        out_bindings[5] = material_texture_binding{
            .texture = env_data->irradiance_cubemap,
            .slot = 5,
            .sampler = {},
            .vertex = false
        };
        out_bindings[6] = material_texture_binding{
            .texture = env_data->prefiltered_cubemap,
            .slot = 6,
            .sampler = {},
            .vertex = false
        };
        out_bindings[7] = material_texture_binding{
            .texture = m_brdf_lut.get(),
            .slot = 7,
            .sampler = {},
            .vertex = false
        };
    }

    // set SRGB GPU format hints on textures that benefit from it
    if(param_data->base_color_tex)
        param_data->base_color_tex->srgb_conversion = true;
    if(param_data->emissive_color_tex)
        param_data->emissive_color_tex->srgb_conversion = true;
}


// Helpers

std::shared_ptr<rtexture> _make_fallback_tex(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    auto tex = std::make_shared<rtexture>(0u);

    auto surf = SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32);
    uint8_t* pixels = static_cast<uint8_t*>(surf->pixels);
    pixels[0] = r;
    pixels[1] = g;
    pixels[2] = b;
    pixels[3] = a;

    tex->load_from(surf);

    return tex;
}


// RTTI

void pbr_mat_controller::_init_rtti()
{
    using namespace ::nb::rtti;

    entt::meta_factory<pbr_mat_params>{}
    .type(entt::hashed_string{"pbr_mat_params"}.value())
    .custom<type_info>(type_info{.identifier = "pbr_mat_params"})
    .data<&pbr_mat_params::base_color_factor, entt::as_ref_t>(entt::hashed_string{"base_color_factor"}.value())
    .custom<data_info>(data_info{.identifier = "base_color_factor", .subtype=DATA_SUBTYPE_COLOR})
    .data<&pbr_mat_params::emissive_color_factor, entt::as_ref_t>(entt::hashed_string{"emissive_color_factor"}.value())
    .custom<data_info>(data_info{.identifier = "emissive_color_factor", .subtype=DATA_SUBTYPE_COLOR})
    .data<&pbr_mat_params::roughness_factor, entt::as_ref_t>(entt::hashed_string{"roughness_factor"}.value())
    .custom<data_info>(data_info{.identifier = "roughness_factor"})
    .data<&pbr_mat_params::metallic_factor, entt::as_ref_t>(entt::hashed_string{"metallic_factor"}.value())
    .custom<data_info>(data_info{.identifier = "metallic_factor"});
}
