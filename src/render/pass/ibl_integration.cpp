#include <newbase/render/pass/ibl_integration.hpp>
#include <newbase/render/mat/ibl_integration.hpp>
#include <newbase/components/environment.hpp>
#include <newbase/res/mesh.hpp>
#include <newbase/res/material.hpp>
#include <newbase/scene.hpp>
#include <algorithm>

using namespace nb;
using namespace nb::render;

build_result ibl_intg_pass_buider::build(pass_build_context &context,
                                         const pass_build_request &request)
{
    bool found_environment = false;
    bool build_failed = false;
    auto environment_view = request.scene.registry().view<cenvironment>();
    for(auto entity : environment_view)
    {
        auto &environment = environment_view.get<cenvironment>(entity);
        if(!environment.preparation_pending() || !environment.equirectangular)
            continue;
        found_environment = true;

        auto *controller = static_cast<ibl_integration_controller*>(
            material_controller::get_controller(material_type::IBL_INTEGRATION));
        if(!controller || !controller->load_shaders())
        {
            build_failed = true;
            continue;
        }

        const uint32_t environment_size =
            std::max(1u, environment.equirectangular->height / 2);
        auto ensure_cubemap = [](std::shared_ptr<rtexture> &texture,
                                 uint32_t size, uint32_t mip_count)
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
        ensure_cubemap(environment.environment_cubemap, environment_size,
                       environment_mip_count);
        ensure_cubemap(environment.irradiance_cubemap, 32, 1);
        ensure_cubemap(environment.prefiltered_cubemap, environment_size,
                       environment_mip_count);

        if(!m_fullscreen_quad)
            m_fullscreen_quad = rmesh::make_fullscreen_quad();
        if(!m_fullscreen_quad)
        {
            build_failed = true;
            continue;
        }

        auto collect_cubemap_passes = [&](pass_type type, rtexture *source,
                                          rtexture *target, uint32_t mip_count)
        {
            for(uint32_t mip = 0; mip < mip_count; ++mip)
            {
                for(uint32_t face = 0; face < 6; ++face)
                {
                    render_pass pass;
                    pass.type = type;
                    pass.target.color_textures[0] = target;
                    pass.targets.color_targets[0] = target->format;
                    pass.targets.color_mip_level[0] = mip;
                    pass.targets.color_layer_index[0] = face;
                    pass.clear_color = true;
                    pass.generate_mipmaps_after_render =
                        type == pass_type::EQUIRECTANGULAR_TO_CUBEMAP &&
                        target->mipmaps.size() > 1 &&
                        mip == mip_count - 1 && face == 5;

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
                        .face_index = face,
                        .mip_level = mip,
                        .mip_count = static_cast<uint32_t>(target->mipmaps.size()),
                        .sample_count = type == pass_type::DIFFUSE_IRRADIANCE
                            ? 4096u : 1024u,
                        .source_resolution = static_cast<float>(source->width),
                        .source_mip_count =
                            std::max(1u, static_cast<uint32_t>(source->mipmaps.size()))
                    };
                    if(!context.append_draw_pass(pass, std::move(material),
                                                 m_fullscreen_quad))
                        return false;
                }
            }
            return true;
        };

        bool passes_collected = collect_cubemap_passes(
            pass_type::EQUIRECTANGULAR_TO_CUBEMAP,
            environment.equirectangular.get(),
            environment.environment_cubemap.get(), 1);
        passes_collected = collect_cubemap_passes(
            pass_type::DIFFUSE_IRRADIANCE,
            environment.environment_cubemap.get(),
            environment.irradiance_cubemap.get(), 1) && passes_collected;
        passes_collected = collect_cubemap_passes(
            pass_type::SPECULAR_PREFILTER,
            environment.environment_cubemap.get(),
            environment.prefiltered_cubemap.get(), environment_mip_count) &&
            passes_collected;
        if(passes_collected)
            environment.mark_prepared();
        else
            build_failed = true;
    }

    if(build_failed)
        return build_result::FAILURE;
    return found_environment ? build_result::SUCCESS : build_result::NOT_APPLICABLE;
}
