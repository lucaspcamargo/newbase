#include "glm/common.hpp"
#include "newbase/render/material.hpp"
#include "newbase/render/pipeline.hpp"
#include "newbase/render/types.hpp"
#include <newbase/render/collector3d.hpp>
#include <newbase/render/mat/ibl_integration.hpp>
#include <newbase/res/material.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/res/texture.hpp>
#include <newbase/engine.hpp>
#include <newbase/scene.hpp>
#include <newbase/components/camera.hpp>
#include <newbase/components/environment.hpp>
#include <newbase/components/spatial.hpp>
#include <newbase/components/mesh.hpp>
#include <newbase/res/mesh.hpp>
#include <algorithm>
#include <entt/core/hashed_string.hpp>

using namespace ::nb;
using namespace ::nb::render;

// NOTE on groups:
// We currently have two groups, OPAQUE and TRANSPARENT

// Sork key layout for OPAQUE group:
//   group    pipeline_idx   material   depth     draw_id
// |--------|--------------|----------|---------|---------|
//   8 bits   16 bits        16 bits    16 bits   8 bits
//
// Here we prioritize avoiding pipeline changes above all else, since they are
// the most expensive thing we can do. TODO: we could re-index pipelines by opaque
// front-to-back, before sorting, to attempt to gain some benefit from early-Z
// fragment discard.

// Sork key layout for TRANSPARENT group:
//   group    ~depth    pipeline_idx   material   draw_id
// |--------|---------|--------------|----------|---------|
//   8 bits   24 bits   12 bits        12 bits    8 bits
//
// For transparency, order is king, so depth gets the most bits and has
// the most priority. The inverted bits of normalized depth are stored (~)!
// In a typical scene it will be the defining factor, but we still try
// to sort by pipeline and material. draw_id remains as the tie-breaker.

// In the future collector could make more use of groups, perhaps using an
// override field in the "layers" component. Or something :)

// Field definitions:
// group:        main command group, opaque vs transparent for now
// pipeline_idx: index of the pipeline in the render command list
// material:     we store some sort of reference to the material here, in an
//               attempt to avoid rebinding textures and uniforms, for example.
// depth:        entitity's spatial distance from the camera, for depth sorting
//               clamped from near to far to 0 -> 1, then normalized into the bit
//               field. transparent depth is stored inverted
// draw_id:      derived from entity IDs, to avoid reorder aliasing

static constexpr uint64_t CMD_3D_GROUP_OPAQUE = 0x40000000'00000000;
static constexpr uint64_t CMD_3D_GROUP_TRANSPARENT = 0xC0000000'00000000;
static constexpr uint64_t CMD_3D_GROUP_MASK = 0xFF000000'00000000;

static constexpr uint64_t CMD_3D_OPAQUE_PIPE_MASK = 0x00FFFF00'00000000;
static constexpr int      CMD_3D_OPAQUE_PIPE_SHFT = (16+16+8);
static constexpr uint64_t CMD_3D_OPAQUE_MATL_MASK = 0x000000FF'FF000000;
static constexpr int      CMD_3D_OPAQUE_MATL_SHFT = (16+8);
static constexpr uint64_t CMD_3D_OPAQUE_DPTH_MASK = 0x00000000'00FFFF00;
static constexpr int      CMD_3D_OPAQUE_DPTH_SHFT = (8);
static constexpr int      CMD_3D_OPAQUE_DPTH_BITS = 16;
static constexpr uint64_t CMD_3D_OPAQUE_DRAW_MASK = 0x00000000'000000FF;
static constexpr int      CMD_3D_OPAQUE_DRAW_SHFT = 0;

static constexpr uint64_t CMD_3D_TRANSP_DPTH_MASK = 0x00FFFFFF'00000000;
static constexpr int      CMD_3D_TRANSP_DPTH_SHFT = (12+12+8);
static constexpr int      CMD_3D_TRANSP_DPTH_BITS = 24;
static constexpr uint64_t CMD_3D_TRANSP_PIPE_MASK = 0x00000000'FFF00000;
static constexpr int      CMD_3D_TRANSP_PIPE_SHFT = (12+8);
static constexpr uint64_t CMD_3D_TRANSP_MATL_MASK = 0x00000000'000FFF00;
static constexpr int      CMD_3D_TRANSP_MATL_SHFT = (8);
static constexpr uint64_t CMD_3D_TRANSP_DRAW_MASK = 0x00000000'000000FF;
static constexpr int      CMD_3D_TRANSP_DRAW_SHFT = 0;


void collector3d::clear()
{
    m_arena.reset();
    m_scene_uniforms.clear();
    m_cmds.clear();
    m_layer_markers.clear();
    m_passes.clear();
    m_pipeline_count = 0;
    m_staging_mesh_count = 0;
    m_staging_texture_count = 0;
    m_staging_shader_count = 0;
    m_staging_ibl_environments.clear();
    m_internal_materials.clear();
}


void collector3d::collect(const render_layer &l, const pass_target_info &layer_targets)
{
    const auto pass_start = static_cast<uint32_t>(m_passes.size());

    auto *scn = engine::instance().find_scene(l.scene_id);
    if(!scn)
    {
        m_layer_markers.push_back(layer_marker {
            .pass_start = pass_start,
            .pass_count = 0
        });
        return;
    }

    auto environment_view = scn->registry().view<cenvironment>();
    for(auto entity : environment_view)
    {
        auto &environment = environment_view.get<cenvironment>(entity);
        if(!environment.preparation_pending())
            continue;
        if(std::find(m_staging_ibl_environments.begin(), m_staging_ibl_environments.end(),
                     &environment) != m_staging_ibl_environments.end())
            continue;
        if(!environment.equirectangular)
            continue;

        auto *controller = static_cast<ibl_integration_controller*>(
            material_controller::get_controller(material_type::IBL_INTEGRATION));
        if(!controller || !controller->load_shaders())
            continue;

        const uint32_t environment_size = std::max(1u, environment.equirectangular->height / 2);
        auto ensure_cubemap = [](std::shared_ptr<rtexture> &texture, uint32_t size, uint32_t mip_count)
        {
            if(!texture)
                texture = std::make_shared<rtexture>();
            auto &target = *texture;
            const bool changed = target.type != texture_type::CUBEMAP ||
                target.format != texture_format::COLOR_RGBA_16_FLOAT ||
                target.width != size || target.height != size ||
                target.layer_count != 6 || target.mipmaps.size() != mip_count;
            target.type = texture_type::CUBEMAP;
            target.format = texture_format::COLOR_RGBA_16_FLOAT;
            target.width = size;
            target.height = size;
            target.depth = 1;
            target.layer_count = 6;
            target.mipmaps.resize(mip_count);
            target.flags |= texture_flags::TARGET;
            if(changed)
            {
                target.flags |= texture_flags::NEEDS_UPLOAD;
                target.flags &= ~texture_flags::UPLOADED;
            }
        };

        uint32_t environment_mip_count = 1;
        for(uint32_t size = environment_size; size > 1; size >>= 1)
            ++environment_mip_count;
        ensure_cubemap(environment.environment_cubemap, environment_size, environment_mip_count);
        ensure_cubemap(environment.irradiance_cubemap, 32, 1);
        ensure_cubemap(environment.prefiltered_cubemap, environment_size, environment_mip_count);

        auto collect_cubemap_passes = [&](pass_type type, rtexture *source, rtexture *target,
                                          uint32_t mip_count)
        {
            for(uint32_t mip = 0; mip < mip_count; ++mip)
            {
                for(uint32_t face = 0; face < 6; ++face)
                {
                    render_pass integration;
                    integration.type = type;
                    integration.target.color_textures[0] = target;
                    integration.targets.color_targets[0] = target->format;
                    integration.targets.color_mip_level[0] = mip;
                    integration.targets.color_layer_index[0] = face;
                    integration.clear_color = true;
                    integration.generate_mipmaps_after_render =
                        type == pass_type::EQUIRECTANGULAR_TO_CUBEMAP &&
                        target->mipmaps.size() > 1 &&
                        mip == mip_count - 1 && face == 5;
                    if(!_collect_integration_pass(integration, source))
                        return false;
                }
            }
            return true;
        };

        bool integration_passes_collected =
            collect_cubemap_passes(pass_type::EQUIRECTANGULAR_TO_CUBEMAP,
                                   environment.equirectangular.get(),
                                   environment.environment_cubemap.get(), 1);
        integration_passes_collected =
            collect_cubemap_passes(pass_type::DIFFUSE_IRRADIANCE,
                                   environment.environment_cubemap.get(),
                                   environment.irradiance_cubemap.get(), 1) &&
            integration_passes_collected;
        integration_passes_collected =
            collect_cubemap_passes(pass_type::SPECULAR_PREFILTER,
                                   environment.environment_cubemap.get(),
                                   environment.prefiltered_cubemap.get(), environment_mip_count) &&
            integration_passes_collected;
        if(integration_passes_collected)
        {
            _add_staging_ibl_environment(&environment);
        }
    }

    mat_env_data env_data {};
    for(auto entity : environment_view)
    {
        auto &environment = environment_view.get<cenvironment>(entity);
        const bool preparation_scheduled =
            std::find(m_staging_ibl_environments.begin(), m_staging_ibl_environments.end(),
                      &environment) != m_staging_ibl_environments.end();
        if((environment.preparation_pending() && !preparation_scheduled) ||
           !environment.environment_cubemap ||
           !environment.irradiance_cubemap ||
           !environment.prefiltered_cubemap)
            continue;
        env_data.ibl_enabled = true;
        env_data.environment_cubemap = environment.environment_cubemap.get();
        env_data.irradiance_cubemap = environment.irradiance_cubemap.get();
        env_data.prefiltered_cubemap = environment.prefiltered_cubemap.get();
        break;
    }

    // Scene preparation passes are ordered before the layer's forward pass.
    render_pass forward;
    forward.type = pass_type::FORWARD;
    forward.target.managed_target = l.target_id;
    forward.targets = layer_targets;
    _collect_pass(l, forward, env_data.ibl_enabled ? &env_data : nullptr);

    m_layer_markers.push_back(layer_marker {
        .pass_start = pass_start,
        .pass_count = static_cast<uint32_t>(m_passes.size() - pass_start)
    });
}

bool collector3d::_collect_integration_pass(const render_pass &pass, rtexture *source)
{
    auto *controller = static_cast<ibl_integration_controller*>(
        material_controller::get_controller(material_type::IBL_INTEGRATION));
    if(!controller || !source)
        return false;

    auto vertex_shader = controller->get_vertex_program(pipeline_descriptor{});
    if(!vertex_shader)
    {
        if(!controller->load_shaders())
            return false;
        vertex_shader = controller->get_vertex_program(pipeline_descriptor{});
    }
    if(!vertex_shader)
        return false;

    if(!m_fullscreen_quad)
        m_fullscreen_quad = rmesh::make_fullscreen_quad();
    if(!m_fullscreen_quad)
        return false;

    auto material = std::make_shared<rmaterial>();
    material->mat_type = material_type::IBL_INTEGRATION;
    material->pipeline_params = pipeline_params {
        .blend = blendmode::NONE,
        .cull = cull_mode::NONE,
        .depth_test = false,
        .depth_write = false
    };
    material->mat_params = ibl_integration_params {
        .source = source,
        .face_index = pass.targets.color_layer_index[0],
        .mip_level = pass.targets.color_mip_level[0],
        .mip_count = pass.target.color_textures[0]
            ? static_cast<uint32_t>(pass.target.color_textures[0]->mipmaps.size()) : 1,
        .sample_count = pass.type == pass_type::DIFFUSE_IRRADIANCE ? 4096u : 1024u,
        .source_resolution = static_cast<float>(source->width),
        .source_mip_count = std::max(1u, static_cast<uint32_t>(source->mipmaps.size()))
    };
    m_internal_materials.push_back(material);

    pipeline_descriptor pd {};
    pd.targets = pass.targets;
    controller->make_pipeline_desc(*material, nullptr, pass.type,
                                   m_fullscreen_quad->vertex_type(), pd);
    const auto pipeline_index = _get_pipeline_index(pd);

    auto &collected = m_passes.emplace_back(collected_pass {
        .desc_offset = UINT32_MAX,
        .cmd_start = static_cast<uint32_t>(m_cmds.size()),
        .cmd_count = 0,
        .scene_uniform_index = static_cast<uint32_t>(m_scene_uniforms.size())
    });
    scene_uniform_std scene_uniform {};
    scene_uniform.view = glm::mat4(1.0f);
    scene_uniform.proj = glm::mat4(1.0f);
    scene_uniform.view_proj = glm::mat4(1.0f);
    scene_uniform.inv_view = glm::mat4(1.0f);
    m_scene_uniforms.push_back(scene_uniform);
    collected.desc_offset = m_arena.byte_offset(m_arena.alloc<render_pass>(pass));

    const auto vertex_uniform_size = controller->get_vertex_ubo_size(pd, nullptr);
    const auto fragment_uniform_size = controller->get_fragment_ubo_size(pd, nullptr);
    uint32_t vertex_uniform_offset = UINT32_MAX;
    uint32_t fragment_uniform_offset = UINT32_MAX;
    if(vertex_uniform_size)
    {
        auto *uniform_data = static_cast<uint8_t*>(m_arena.alloc_raw(nullptr, vertex_uniform_size));
        controller->pack_vertex_uniforms(*material, pd, nullptr, uniform_data, vertex_uniform_size);
        vertex_uniform_offset = m_arena.byte_offset(uniform_data);
    }
    if(fragment_uniform_size)
    {
        auto *uniform_data = static_cast<uint8_t*>(m_arena.alloc_raw(nullptr, fragment_uniform_size));
        controller->pack_fragment_uniforms(*material, pd, nullptr, uniform_data, fragment_uniform_size);
        fragment_uniform_offset = m_arena.byte_offset(uniform_data);
    }

    const auto texture_binding_count =
        static_cast<uint8_t>(controller->get_num_texture_bindings(*material, pd, nullptr));
    uint32_t texture_bindings_offset = UINT32_MAX;
    if(texture_binding_count)
    {
        auto *bindings = static_cast<material_texture_binding*>(
            m_arena.alloc_raw(nullptr, texture_binding_count * sizeof(material_texture_binding)));
        controller->collect_texture_bindings(*material, pd, nullptr, bindings);
        texture_bindings_offset = m_arena.byte_offset(bindings);
        for(uint8_t i = 0; i < texture_binding_count; ++i)
            if(bindings[i].texture)
                _add_staging_texture(bindings[i].texture);
    }

    for(auto *texture : pass.target.color_textures)
        if(texture)
            _add_staging_texture(texture);
    if(pass.target.depth_texture)
        _add_staging_texture(pass.target.depth_texture);

    _add_staging_mesh(m_fullscreen_quad.get());
    _add_staging_shader(controller->get_vertex_program(pd).get());
    _add_staging_shader(controller->get_fragment_program(pd).get());

    m_cmds.emplace_back(command3d {
        .sort_key = 0,
        .material = material.get(),
        .mat_vert_uniforms_offset = vertex_uniform_offset,
        .mat_vert_uniforms_len = static_cast<uint32_t>(vertex_uniform_size),
        .mat_frag_uniforms_offset = fragment_uniform_offset,
        .mat_frag_uniforms_len = static_cast<uint32_t>(fragment_uniform_size),
        .model_uniforms_offset = UINT32_MAX,
        .pipeline_index = pipeline_index,
        .mesh_index = _add_staging_mesh(m_fullscreen_quad.get()),
        .submesh = 0,
        ._pad0 = 0,
        .tex_bind_count = texture_binding_count,
        .tex_binds_offset = texture_bindings_offset
    });
    collected.cmd_count = 1;
    return true;
}

void collector3d::_collect_pass(const render_layer &l, const render_pass &pass,
                                const mat_env_data *env_data)
{
    auto cmd_start = (uint32_t) m_cmds.size();

    // get some basic layer info
    float aspect = l.viewport.w/(float)l.viewport.h;

    // get scene
    auto *scn = engine::instance().find_scene(l.scene_id);
    if(!scn)
    {
        return;
    }

    // prepare scene uniforms and projection
    render::scene_uniform_std u_scn;
    auto cam_eid = l.camera;
    auto cam = scn->registry().try_get<ccamera>(cam_eid);
    auto cam_sp = scn->registry().try_get<cspatial>(cam_eid);
    if(cam && cam_sp)
    {
        auto proj = glm::perspective(cam->cam3d.fov, aspect,
                                     cam->cam3d.clip_near, cam->cam3d.clip_far);
        auto view = glm::inverse(cam_sp->world);
        u_scn.view = view;
        u_scn.proj = proj;
        u_scn.view_proj = proj*view;
        u_scn.inv_view = glm::inverse(view);
        u_scn.camera_pos = cam_sp->pos;
    }
    // HACK temporary, this will change
    u_scn.light_color = glm::vec3{1.8f, 1.8f, 1.5f};        // TODO
    u_scn.light_dir = glm::vec3{0.0f, 1.0f, 0.5f}; // TODO
    const uint32_t scene_uniform_index = static_cast<uint32_t>(m_scene_uniforms.size());
    m_scene_uniforms.push_back(u_scn);

    render_pass pass_desc = pass;
    bool has_explicit_target = pass_desc.target.depth_texture != nullptr;
    for(auto *texture : pass_desc.target.color_textures)
        has_explicit_target = has_explicit_target || texture != nullptr;
    if(pass_desc.target.managed_target == render::TARGET_INVALID && !has_explicit_target)
        pass_desc.target.managed_target = l.target_id;
    for(auto *texture : pass_desc.target.color_textures)
        if(texture)
            _add_staging_texture(texture);
    if(pass_desc.target.depth_texture)
        _add_staging_texture(pass_desc.target.depth_texture);

    const auto pass_desc_offset = m_arena.byte_offset(m_arena.alloc<render_pass>(pass_desc));
    const uint32_t pass_index = static_cast<uint32_t>(m_passes.size());
    m_passes.push_back(collected_pass {
        .desc_offset = pass_desc_offset,
        .cmd_start = cmd_start,
        .cmd_count = 0,
        .scene_uniform_index = scene_uniform_index
    });

    // iterate over meshes
    auto mesh_view = scn->registry().view<cmesh, cspatial>();
    for(const auto &[eid, msh, spa]: mesh_view.each())
    {
        if(!msh.mesh || !msh.mesh->submeshes().size())
            continue;

        // stage mesh
        const auto msh_idx = _add_staging_mesh(msh.mesh.get());

        const model_uniforms_std * u_model = m_arena.alloc<model_uniforms_std>(
            spa.world,
            glm::transpose(glm::inverse(spa.world))
        );
        const auto u_model_offset = m_arena.byte_offset(u_model);

        for (int sub_i = 0; sub_i < msh.mesh->submeshes().size(); ++sub_i)
        {
            const auto &subm = msh.mesh->submeshes()[sub_i];
            if(!subm.index_count)
                continue;

            assert(subm.material_idx < cmesh::MAX_MATERIALS && "Material index past max mesh component entries!");
            const auto &mat_ptr = msh.materials[subm.material_idx];

            if(!mat_ptr)
                continue; // NOTE fallback material support goes here

            auto &material = *mat_ptr;

            auto mat_ctrl = material_controller::get_controller(material.mat_type);
            if(!mat_ctrl)
                continue;

            // prepare pipeline descriptor
            pipeline_descriptor pd;
            pd.targets = pass_desc.targets;
            mat_ctrl->make_pipeline_desc(material, env_data, pass_desc.type,
                                         msh.mesh->vertex_type(), pd);
            auto pd_index = _get_pipeline_index(pd);

            // make material vert uniforms
            auto u_vtx_sz = mat_ctrl->get_vertex_ubo_size(pd, env_data);
            auto u_vtx_offset = 0;
            if(u_vtx_sz)
            {
                auto alloc = m_arena.alloc_raw(nullptr, u_vtx_sz);
                mat_ctrl->pack_vertex_uniforms(material, pd, env_data,
                                               (uint8_t*) alloc, u_vtx_sz);
                u_vtx_offset = m_arena.byte_offset(alloc);
            }

            // make material frag uniforms
            auto u_frag_sz = mat_ctrl->get_fragment_ubo_size(pd, env_data);
            auto u_frag_offset = 0;
            if(u_frag_sz)
            {
                auto alloc = m_arena.alloc_raw(nullptr, u_frag_sz);
                mat_ctrl->pack_fragment_uniforms(material, pd, env_data,
                                                 (uint8_t*) alloc, u_frag_sz);
                u_frag_offset = m_arena.byte_offset(alloc);
            }

            uint8_t tex_bind_count = mat_ctrl->get_num_texture_bindings(material, pd, env_data);
            uint32_t tex_bind_offset = 0;
            if(tex_bind_count)
            {
                auto alloc = m_arena.alloc_raw(nullptr, tex_bind_count*sizeof(material_texture_binding));
                material_texture_binding* bind_dest = (material_texture_binding*) alloc;
                mat_ctrl->collect_texture_bindings(material, pd, env_data, bind_dest);
                tex_bind_offset = m_arena.byte_offset(alloc);

                // stage textures
                for(int bi = 0; bi < tex_bind_count; bi++)
                {
                    const auto tex_ptr = bind_dest[bi].texture;
                    if(tex_ptr)
                        _add_staging_texture(tex_ptr);
                }
            }

            // stage shaders
            _add_staging_shader(mat_ctrl->get_vertex_program(pd).get());
            _add_staging_shader(mat_ctrl->get_fragment_program(pd).get());

            // calculate mesh center depth
            auto mesh_center_proj = u_scn.view_proj * glm::vec4(spa.pos, 1.0f);
            auto depth_norm = glm::clamp(mesh_center_proj.z, 0.0f, 1.0f);

            // build command sort key
            uint64_t sort_key {0};
            if(pd.params.blend != blendmode::NONE)
            {
                // transparent
                sort_key = CMD_3D_GROUP_TRANSPARENT;
                uint64_t depth_bits = depth_norm * ((1<<CMD_3D_TRANSP_DPTH_BITS)-1);
                sort_key |= CMD_3D_TRANSP_DPTH_MASK & ~(depth_bits << CMD_3D_TRANSP_DPTH_SHFT);
                sort_key |= CMD_3D_TRANSP_PIPE_MASK & (((uint64_t)pd_index) << CMD_3D_TRANSP_PIPE_SHFT);
                sort_key |= CMD_3D_TRANSP_MATL_MASK & (((uintptr_t)mat_ptr.get()) << (CMD_3D_TRANSP_MATL_SHFT-2));
                const auto eid_int = (uint32_t) eid;
                const auto eid_mix = eid_int ^ (eid_int >> 8);
                sort_key |= CMD_3D_TRANSP_DRAW_MASK & (eid_mix << CMD_3D_TRANSP_DRAW_SHFT);
            }
            else
            {
                // opaque
                sort_key = CMD_3D_GROUP_OPAQUE;
                sort_key |= CMD_3D_OPAQUE_PIPE_MASK & (((uint64_t)pd_index) << CMD_3D_OPAQUE_PIPE_SHFT);
                sort_key |= CMD_3D_OPAQUE_MATL_MASK & (((uintptr_t)mat_ptr.get()) << (CMD_3D_OPAQUE_MATL_SHFT-2));
                uint64_t depth_bits = depth_norm * ((1<<CMD_3D_OPAQUE_DPTH_BITS)-1);
                sort_key |= CMD_3D_OPAQUE_DPTH_MASK & ~(depth_bits << CMD_3D_OPAQUE_DPTH_SHFT);
                const auto eid_int = (uint32_t) eid;
                const auto eid_mix = eid_int ^ (eid_int >> 8);
                sort_key |= CMD_3D_OPAQUE_DRAW_MASK & (eid_mix << CMD_3D_OPAQUE_DRAW_SHFT);
            }

            // add command
            m_cmds.emplace_back( command3d{
                .sort_key = sort_key,
                .material = &material,
                .mat_vert_uniforms_offset = (uint32_t) u_vtx_offset,
                .mat_vert_uniforms_len = (uint32_t) u_vtx_sz,
                .mat_frag_uniforms_offset = (uint32_t) u_frag_offset,
                .mat_frag_uniforms_len = (uint32_t) u_frag_sz,
                .model_uniforms_offset = u_model_offset,
                .pipeline_index = pd_index,
                .mesh_index = msh_idx,
                .submesh = (uint16_t)sub_i,
                ._pad0 = 0,
                .tex_bind_count = tex_bind_count,
                .tex_binds_offset = tex_bind_offset
            } );
        }
    }

    auto &collected = m_passes[pass_index];
    collected.cmd_count = static_cast<uint32_t>(m_cmds.size() - cmd_start);
    _sort(collected);
}

void collector3d::_add_staging_ibl_environment(cenvironment *environment)
{
    if(environment &&
       std::find(m_staging_ibl_environments.begin(), m_staging_ibl_environments.end(), environment) ==
           m_staging_ibl_environments.end())
        m_staging_ibl_environments.push_back(environment);
}

// sorts rendering commands belonging to one pass
void collector3d::_sort(const collected_pass &pass)
{
    if(!pass.cmd_count)
        return;

    auto begin = m_cmds.begin() + pass.cmd_start;
    auto end = begin + pass.cmd_count;

    // TODO we can move less memory around and do better than this with radix sort, but later
    std::sort(begin, end, +[](const command3d &a, const command3d &b)->bool{
        return a.sort_key < b.sort_key;
    });
}

uint16_t collector3d::_get_pipeline_index(const pipeline_descriptor & desc)
{
    const auto hash = desc.hash();
    auto it = std::find(m_pipeline_hashes.begin(), m_pipeline_hashes.end(), hash);
    if(it != m_pipeline_hashes.end())
    {
        auto ret_idx = (uint16_t) (it - m_pipeline_hashes.begin());
        assert(m_pipeline_descs[ret_idx] == desc); // check for collisions in debug mode
        return ret_idx;
    }
    assert(m_pipeline_count < COLLECTOR_3D_MAX_PIPELINES);
    m_pipeline_hashes[m_pipeline_count] = hash;
    m_pipeline_descs[m_pipeline_count]  = desc;
    return m_pipeline_count++;
}

uint16_t collector3d::_add_staging_texture(rtexture* tex)
{
    auto begin = m_staging_textures.begin();
    auto end = begin + m_staging_texture_count;
    auto it = std::find(begin, end, tex);
    if(it != end)
        return (uint16_t) (it - begin);
    assert(m_staging_texture_count < COLLECTOR_3D_MAX_TEXTURES);
    m_staging_textures[m_staging_texture_count] = tex;
    return m_staging_texture_count++;
}

uint16_t collector3d::_add_staging_shader(rshader* shd)
{
    auto begin = m_staging_shaders.begin();
    auto end = begin + m_staging_shader_count;
    auto it = std::find(begin, end, shd);
    if(it != end)
        return (uint16_t) (it - begin);
    assert(m_staging_shader_count < COLLECTOR_3D_MAX_SHADERS);
    m_staging_shaders[m_staging_shader_count] = shd;
    return m_staging_shader_count++;
}

uint16_t collector3d::_add_staging_mesh(rmesh* msh)
{
    auto begin = m_staging_meshes.begin();
    auto end = begin + m_staging_mesh_count;
    auto it = std::find(begin, end, msh);
    if(it != end)
        return (uint16_t) (it - begin);
    assert(m_staging_mesh_count < COLLECTOR_3D_MAX_MESHES);
    m_staging_meshes[m_staging_mesh_count] = msh;
    return m_staging_mesh_count++;
}
