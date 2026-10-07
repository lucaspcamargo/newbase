#include <newbase/render/material.hpp>
#include <newbase/render/types.hpp>
#include <newbase/render/mat/pbr.hpp>
#include <newbase/res/material.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/res/texture.hpp>
#include <newbase/res/gltf.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/reflection/resources.hpp>
#include <newbase/log.hpp>

#include <entt/core/fwd.hpp>
#include <entt/core/hashed_string.hpp>
#include <entt/meta/meta.hpp>
#include <tiny_gltf_v3.h>
#include <string>
#include <memory>


using namespace nb;
using entt::operator""_hs;


bool rmaterial::do_load()
{
    log::info("[rmaterial] loading: 0x%08x", id());

    const auto &rman_handles = rman().handles();
    const auto it = rman_handles.find(id());
    if(it == rman_handles.end())
    {
        log::error("[rmaterial] not in vfs");
        return false;
    }
    const auto &hnd = it->second;
    if(!hnd.parent_id)
    {
        log::error("[rmaterial] no parent resource, can only load from gltf");
        return false;
    }

    const auto rgltf_hs = "rgltf"_hs;
    const auto parent_ref = rman().create(rgltf_hs, hnd.parent_id);
    if(parent_ref->type_id() != rgltf_hs)
    {
        log::error("[rmaterial] parent resource of wrong type!");
        return false;
    }

    rgltf &gltf = *((rgltf*)parent_ref.get());
    gltf.force_preload_sync();

    if(!gltf.has_model())
    {
        log::error("[rmaterial] parent gltf is empty!");
        return false;
    }

    const auto parent_path = hnd.path.substr(0, hnd.path.find("?"));

    auto params_loaded = _load_gltf_model_material(gltf.get_model(),
                gltf.get_model()->materials + (hnd.name[4]-'0'), parent_path); // TODO IDX HACK

    if(!params_loaded)
    {
        log::error("[rmaterial] loding failed!");
        return false;
    }

    // now that we have our params in place, we can materialize (wink wink) render data
    if(!materialize())
    {
        log::warn("[rmaterial] failed to materialize");
        // a material that does not materialize does not fail loading
    }

    // finally, try to load all textures that the material controller gives out for us
    if(m_controller)
    {
        render::material_controller::tex_bind_vec_t tex_binds;
        m_controller->collect_texture_bindings(*this, tex_binds);
        for(auto &bind: tex_binds)
        {
            if(!bind.texture)
                log::warn("[rmaterial] no texture for bind slot: %d", bind.slot);
            else if(!bind.texture->step_load())
            {
                log::warn("[rmaterial] bound texture in transition, loading skipped load");
            }
        }
    }

    return true;
}


bool rmaterial::materialize()
{
    m_controller = render::material_controller::get_controller(mat_type);
    if(!m_controller)
    {
        log::warn("[rmaterial] no controller for type: %d", (int) mat_type);
        return false;
    }

    if(!mat_params)
    {
        log::warn("[rmaterial] empty params!");
        return false;
    }

    auto v_ubo_sz = m_controller->get_vertex_ubo_size(*this);
    if(v_ubo_sz)
    {
        vert_uniforms.resize(v_ubo_sz);
        m_controller->pack_vertex_uniforms(*this, vert_uniforms.data(), v_ubo_sz);
    }
    else
        vert_uniforms.clear();

    auto f_ubo_sz = m_controller->get_fragment_ubo_size(*this);
    if(f_ubo_sz)
    {
        frag_uniforms.resize(f_ubo_sz);
        m_controller->pack_fragment_uniforms(*this, frag_uniforms.data(), f_ubo_sz);
    }
    else
        vert_uniforms.clear();

    dirty = false;
    return true;
}


bool rmaterial::_load_gltf_model_material(const tg3_model *model, const tg3_material *mat, const std::string &parent_path)
{
    // TODO for now, we olny support core metallic-roughtness PBR
    //      at least support unlit in the future, when we have the material type

    mat_type = render::material_type::STANDARD_PBR;
    auto pbr_params = render::pbr_mat_params {};

    // pbr params - TODO resolve sampler configs too

    pbr_params.base_color_tex = _resolve_texture(model, mat->pbr_metallic_roughness.base_color_texture.index, parent_path);
    const auto &base_col = mat->pbr_metallic_roughness.base_color_factor;
    pbr_params.base_color_factor = glm::vec4 { (float) base_col[0],
        (float) base_col[1], (float) base_col[2], (float) base_col[3] };

    pbr_params.metal_rough_tex = _resolve_texture(model, mat->pbr_metallic_roughness.metallic_roughness_texture.index, parent_path);
    pbr_params.metallic_factor = mat->pbr_metallic_roughness.metallic_factor;
    pbr_params.roughness_factor = mat->pbr_metallic_roughness.roughness_factor;

    pbr_params.normal_tex = _resolve_texture(model, mat->normal_texture.index, parent_path);
    pbr_params.normal_scaling = (float) mat->normal_texture.scale;

    pbr_params.ao_tex = _resolve_texture(model, mat->occlusion_texture.index, parent_path);
    pbr_params.occlusion_strength = mat->occlusion_texture.strength;

    pbr_params.emissive_color_tex = _resolve_texture(model, mat->emissive_texture.index, parent_path);
    const auto &emit_col = mat->emissive_factor;
    pbr_params.emissive_color_factor = glm::vec4 { (float) emit_col[0],
        (float) emit_col[1], (float) emit_col[2], 1.0f };

    mat_params = pbr_params;  // set internal meta_any parameter block

    // pipeline params
    pipeline_params.cull_back = !mat->double_sided;

    const std::string_view alpha_mode {mat->alpha_mode.data, mat->alpha_mode.len};
    if(alpha_mode == "BLEND")
        pipeline_params.blend = render::blendmode::BLEND;
    else
        pipeline_params.blend = render::blendmode::NONE;
    // TODO support "MASK" in the shader (with discard)

    return true;
}


std::shared_ptr<rtexture> rmaterial::_resolve_texture(const tg3_model *model, int32_t index, const std::string &parent_path)
{
    if(index == -1)
        return {nullptr};

    const auto &tex_block = model->textures[index];

    if(tex_block.source == -1)
        return {nullptr};

    std::string tex_path = parent_path + "?tex=" + std::to_string(tex_block.source);

    log::warn("RESOLVE TEX name='%s' source=%d", std::string{std::string_view{tex_block.name.data, tex_block.name.len}}.c_str(), tex_block.source);

    return std::static_pointer_cast<rtexture>(rman().create("rtexture"_hs, entt::hashed_string{tex_path.c_str()}.value()));
}

void rmaterial::_setup_sampler(const tg3_model *model, const tg3_texture_info &tex_info, render::sampler_config &sc)
{
    if(tex_info.index == -1)
        return;

    const auto &tex_block = model->textures[tex_info.index];

    if(tex_block.sampler == -1)
        return;

    const auto &sampler_block = model->samplers[tex_block.sampler];

    if(sampler_block.min_filter != -1)
    {
        switch(sampler_block.min_filter)
        {
            case TG3_TEXTURE_FILTER_NEAREST:
                sc.min_filter = render::sampler_filter::NEAREST;
                sc.max_lod = 0.0f;
                break;
            case TG3_TEXTURE_FILTER_LINEAR:
                sc.min_filter = render::sampler_filter::LINEAR;
                sc.max_lod = 0.0f;
                break;
            case TG3_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
                sc.min_filter = render::sampler_filter::NEAREST;
                sc.mip_filter = render::sampler_filter::NEAREST;
                break;
            case TG3_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
                sc.min_filter = render::sampler_filter::LINEAR;
                sc.mip_filter = render::sampler_filter::NEAREST;
                break;
            case TG3_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
                sc.min_filter = render::sampler_filter::NEAREST;
                sc.mip_filter = render::sampler_filter::LINEAR;
                break;
            case TG3_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
                sc.min_filter = render::sampler_filter::LINEAR;
                sc.mip_filter = render::sampler_filter::LINEAR;
                break;
        }
    }

    if(sampler_block.mag_filter != -1)
    {
        switch(sampler_block.mag_filter)
        {
            case TG3_TEXTURE_FILTER_NEAREST:
                sc.mag_filter = render::sampler_filter::NEAREST;
                break;
            case TG3_TEXTURE_FILTER_LINEAR:
                sc.mag_filter = render::sampler_filter::NEAREST;
                break;
        }
    }

    if(sampler_block.wrap_s != -1)
    {
        switch(sampler_block.wrap_s)
        {
            case TG3_TEXTURE_WRAP_REPEAT:
                sc.addr_mode_u = render::sampler_addressing::REPEAT;
                break;
            case TG3_TEXTURE_WRAP_CLAMP_TO_EDGE:
                sc.addr_mode_u = render::sampler_addressing::CLAMP_TO_EDGE;
                break;
            case TG3_TEXTURE_WRAP_MIRRORED_REPEAT:
                sc.addr_mode_u = render::sampler_addressing::REPEAT_MIRRORED;
                break;
        }
    }
    else
        sc.addr_mode_u = render::sampler_addressing::REPEAT;

    if(sampler_block.wrap_t != -1)
    {
        switch(sampler_block.wrap_t)
        {
            case TG3_TEXTURE_WRAP_REPEAT:
                sc.addr_mode_v = render::sampler_addressing::REPEAT;
                break;
            case TG3_TEXTURE_WRAP_CLAMP_TO_EDGE:
                sc.addr_mode_v = render::sampler_addressing::CLAMP_TO_EDGE;
                break;
            case TG3_TEXTURE_WRAP_MIRRORED_REPEAT:
                sc.addr_mode_v = render::sampler_addressing::REPEAT_MIRRORED;
                break;
        }

    }
    else
        sc.addr_mode_v = render::sampler_addressing::REPEAT;
}

// RTTI

void rmaterial::_init_rtti()
{
    using namespace rtti;

    entt::meta_factory<rmaterial>{}
    .type(entt::hashed_string{"rmaterial"}.value())
    .custom<rtti::type_info>(rtti::type_info{
        .identifier = "material",
        .type_class = nb::rtti::TYPE_CLASS_RESOURCE,
        .data = {.resource = {.editor_icon = nullptr, .extensions = "",
            .factory_fn = +[](entt::id_type id) -> std::shared_ptr<nb::resource>
            {
                return std::make_shared<rmaterial>(id);
            }
        }}
    });

    // TODO data fields

    rtti::res_ptr_registration<rmaterial>("rmaterial");
}
