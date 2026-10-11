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


static std::shared_ptr<rtexture> _make_fallback_tex(uint8_t r, uint8_t g, uint8_t b, uint8_t a);


pbr_mat_controller::pbr_mat_controller()
{
    log::info("[pbr] constructing");
    m_vert = rman().load_sync<rshader>("_nb_core/slang/pbr.vert.slang"_hs);
    m_frag = rman().load_sync<rshader>("_nb_core/slang/pbr.frag.slang"_hs);

    m_fallback_color    = _make_fallback_tex(255, 255, 255, 255); // factor control
    m_fallback_normal   = _make_fallback_tex(128, 128, 255, 255);
    m_fallback_metal    = _make_fallback_tex(255, 255, 255, 255); // factor control
    m_fallback_ao       = _make_fallback_tex(255, 255, 255, 255); // no ao
    m_fallback_emissive = _make_fallback_tex(255, 255, 255, 255); // factor control
}

pbr_mat_controller::~pbr_mat_controller()
{
    log::info("[pbr] destroying");
    m_vert.reset();
    m_frag.reset();

    m_fallback_color.reset();
    m_fallback_normal.reset();
    m_fallback_metal.reset();
    m_fallback_ao.reset();
    m_fallback_emissive.reset();
}


void pbr_mat_controller::make_pipeline_desc(const rmaterial& mat, pass_type pt,
                        vertex_type vt, pipeline_descriptor &dst_desc) const
{
    dst_desc.mat_type = material_type::STANDARD_PBR;
    dst_desc.params = mat.pipeline_params;
    dst_desc.shader_variant = 0;
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
    return m_frag;
}

size_t pbr_mat_controller::get_vertex_ubo_size(const pipeline_descriptor &dst_desc) const
{
    return 0;
}

size_t pbr_mat_controller::get_fragment_ubo_size(const pipeline_descriptor &dst_desc) const
{
    return sizeof(pbr_frag_uniforms);
}

void pbr_mat_controller::pack_vertex_uniforms(const rmaterial &mat,
                                              const pipeline_descriptor &dst_desc,
                                              uint8_t *out_buffer,
                                              size_t out_len) const
{
}

void pbr_mat_controller::pack_fragment_uniforms(const rmaterial &mat,
                                                const pipeline_descriptor &dst_desc,
                                                uint8_t *out_buffer,
                                                size_t out_len) const
{
    assert(out_len == sizeof(pbr_frag_uniforms));

    const auto param_data = mat.mat_params.try_cast<const pbr_mat_params>();
    if(!param_data)
    {
        log::warn("[pbr] collect_texture_bindings: wrong parameter block type!");
        return;
    }

    // get params from mat
    pbr_frag_uniforms &dest = *(pbr_frag_uniforms*) out_buffer;
    dest.base_color_factor = param_data->base_color_factor;
    dest.emissive_factor = param_data->emissive_color_factor;
    dest.metallic_factor = param_data->metallic_factor;
    dest.roughness_factor = param_data->roughness_factor;
    dest.normal_scaling  = param_data->normal_scaling;
    dest.occlusion_strength = param_data->occlusion_strength;
    dest._pad = 0.0f;
}


size_t pbr_mat_controller::get_num_texture_bindings(const rmaterial& mat, const pipeline_descriptor &dest) const
{
    return 5;
}

void pbr_mat_controller::collect_texture_bindings(const rmaterial &mat,
                                                  const pipeline_descriptor &dst_desc,
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
        .sampler = param_data->normal_sampling,
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
