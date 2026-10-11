#include "newbase/render/material.hpp"
#include "newbase/render/pass.hpp"
#include "newbase/render/pipeline.hpp"
#include "newbase/render/types.hpp"
#include "newbase/render/vertex.hpp"
#include <newbase/res/shader.hpp>
#include <newbase/sys/render_gpu/pipelines.hpp>
#include <newbase/log.hpp>
#include <SDL3/SDL_gpu.h>
#include <cassert>


namespace nb::gpu
{
    inline SDL_GPUVertexElementFormat to_sdl_format(render::vertex_data_type type)
    {
        using namespace render;
        switch (type) {
            case vertex_data_type::FLOAT2: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
            case vertex_data_type::FLOAT3: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
            case vertex_data_type::FLOAT4: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
            default: return SDL_GPU_VERTEXELEMENTFORMAT_INVALID;
        }
    }


    bool setup_vertex_input(SDL_GPUVertexInputState &input_state,
                        SDL_GPUVertexBufferDescription *buf_descs,
                        SDL_GPUVertexAttribute *attr_descs,
                        const render::vertex_type_descriptor& cpu_vertex_desc,
                        const render::shader_reflection& vert_shader_reflection)
    {
        // setup buffers
        // now fixed at one buffer
        SDL_GPUVertexBufferDescription& buf0 = buf_descs[0];
        buf0.slot = 0;
        buf0.pitch = cpu_vertex_desc.size;
        buf0.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        buf0.instance_step_rate = 0;
        input_state.num_vertex_buffers = 1;

        int vert_attr_count = 0;
        for (const auto& shader_attr : vert_shader_reflection.attributes)
        {
            if (vert_attr_count >= MAX_VERTEX_ATTRIBUTES) {
                log::error("[render_gpu] pipeline_desc: setup_vertex_input: maximum number of vertex attributes exceeded: desc type (%d)",
                     (int)cpu_vertex_desc.type);
                return false;
            }

            render::vertex_attribute_type semantic = render::conv::name_to_vertex_attribute(shader_attr.name);
            bool assigned = false;
            for (const auto& cpu_attr : cpu_vertex_desc.attributes)
            {
                if (cpu_attr.attr == semantic)
                {
                    SDL_GPUVertexAttribute& attr = attr_descs[vert_attr_count++];
                    attr.location = shader_attr.location;
                    attr.buffer_slot = 0;
                    attr.format = to_sdl_format(cpu_attr.format);
                    if(attr.format == SDL_GPU_VERTEXELEMENTFORMAT_INVALID){
                        log::error("[render_gpu] pipeline_desc: setup_vertex_input: cpu attr format %d cannot map to SDL format!", (int) cpu_attr.format);
                        return false;
                    }
                    attr.offset = cpu_attr.offset;
                    assigned = true;
                    break;
                }
            }
            if(!assigned)
            {
                log::warn("[render_gpu] pipeline_desc: setup_vertex_input: vertex shader input attr '%s' (semantic %d) has no source!", shader_attr.name.c_str(), (int)semantic);
            }
        }
        input_state.num_vertex_attributes = vert_attr_count;

        return true;
    }


    void setup_material_pipeline_props(SDL_GPURasterizerState &state, const render::pipeline_params &pp)
    {
        switch (pp.cull)
        {
            case render::cull_mode::BACK:
                state.cull_mode = SDL_GPU_CULLMODE_BACK;
                break;
            case render::cull_mode::FRONT:
                state.cull_mode = SDL_GPU_CULLMODE_FRONT;
                break;
            case render::cull_mode::INVALID:
                [[fallthrough]];
            case render::cull_mode::NONE:
                [[fallthrough]];
            default:
                state.cull_mode = SDL_GPU_CULLMODE_NONE;
                break;
        }

        state.fill_mode = SDL_GPU_FILLMODE_FILL;
        state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
        state.enable_depth_bias = false;
        state.depth_bias_constant_factor = 0.0f;
        state.depth_bias_clamp = 0.0f;
        state.depth_bias_slope_factor = 0.0f,
        state.enable_depth_clip = true;
    }


    pipeline_object* pipeline_cache::find_or_create_prehashed(SDL_GPUDevice *gdev,
                const render::pipeline_descriptor& descriptor, hash_t hash)
    {
        auto it = m_cache.find(hash);
        if (it != m_cache.end()) {
            #ifndef NDEBUG
            if (it->second.descriptor != descriptor) {
                assert(!"CRITICAL: 64-bit pipeline hash collision detected!");
            }
            #endif
            return &it->second;
        }

        auto gpu_pipeline = _create_pipeline(gdev, descriptor, hash);
        if(!gpu_pipeline)
        {
            log::error("[render_gpu] pipelines: failed to create pipeline 0x%016x: %s", hash, SDL_GetError());
            return nullptr;
        }

        // not in cache, create and return pipeline object
        auto new_it = m_cache.emplace(hash, pipeline_object{
            hash,
            gpu_pipeline
            #ifndef NDEBUG
            , descriptor
            #endif
        });

        // return address of newly-created pipeline object
        return &(new_it.first->second);
    }

    pipeline_object* pipeline_cache::find_or_create(SDL_GPUDevice *gdev, const render::pipeline_descriptor& descriptor)
    {
        assert(gdev && "[pipeline_cache] no GPU device!");

        auto hash = descriptor.hash();
        if(!hash)
            return nullptr; // invalid descriptor!

        return find_or_create_prehashed(gdev, descriptor, hash);
    }

    void pipeline_cache::clear(SDL_GPUDevice *gdev)
    {
        for (auto& [key, pipeline] : m_cache) {
            if (pipeline.valid())
            {
                SDL_ReleaseGPUGraphicsPipeline(gdev, pipeline.pipeline);
            }
        }
        m_cache.clear();
    }


    void pipeline_cache::evict_by_shader(SDL_GPUDevice *gdev, SDL_GPUShader *shader)
    {
        // go over the pipeline state hash mappings for the shader being destroyed
        // release all pipeline state objects using this shader
        // finish off by removing the hash mappings for the shader

        auto it = m_shader_map.find(shader);
        if(it == m_shader_map.end())
            return; //nothing to evict

        auto &mapping = it->second;
        for(uint32_t i = 0; i < mapping.inl_count; i++)
        {
            auto it_spill = m_cache.find(mapping.inl[i]);
            if(it_spill != m_cache.end())
            {
                if(it_spill->second.valid())
                    SDL_ReleaseGPUGraphicsPipeline(gdev, it_spill->second.pipeline);
                m_cache.erase(it_spill);
            }
        }
        for(auto hash : it->second.spill)
        {
            auto it_spill = m_cache.find(hash);
            if(it_spill != m_cache.end())
            {
                if(it_spill->second.valid())
                    SDL_ReleaseGPUGraphicsPipeline(gdev, it_spill->second.pipeline);
                m_cache.erase(it_spill);
            }
        }
        m_shader_map.erase(it);
    }


    // our blend conversion helper
    static SDL_GPUColorTargetBlendState convert_blend_mode(render::blendmode mode);


    SDL_GPUGraphicsPipeline* pipeline_cache::_create_pipeline(SDL_GPUDevice *gdev, const render::pipeline_descriptor &desc, hash_t hash)
    {
        auto *mat_ctrl = render::material_controller::get_controller(desc.mat_type);
        auto *res_vert = mat_ctrl->get_vertex_program(desc).get();
        auto *res_frag = mat_ctrl->get_fragment_program(desc).get();

        SDL_GPUShader *vert = res_vert? (SDL_GPUShader*)res_vert->rptr : nullptr;
        SDL_GPUShader *frag = res_frag? (SDL_GPUShader*)res_frag->rptr : nullptr;

        if(!vert || !frag)
        {
            log::warn("[render_gpu] pipeline_cache: _create_pipeline: shaders are not ready!");
            return nullptr;
        }

        // render target and blend info
        SDL_GPUColorTargetDescription color_targets[render::PASS_MAX_COLOR_TARGETS]{};
        SDL_GPUGraphicsPipelineTargetInfo target_info = {
            .color_target_descriptions = color_targets
        };
        auto ret_targets = m_targets.prepare_gpu_pipeline_target_info(target_info, color_targets, desc.targets);
        if(desc.params.blend != render::blendmode::NONE && target_info.num_color_targets)
            color_targets[0].blend_state = convert_blend_mode(desc.params.blend);
        if(!ret_targets)
        {
            log::warn("[render_gpu] pipeline_cache: _create_pipeline: failed to setup target info!");
            return nullptr;
        }


        // vertex input
        SDL_GPUVertexBufferDescription buf_desc[MAX_VERTEX_BUFFERS];
        SDL_GPUVertexAttribute attr_desc[MAX_VERTEX_ATTRIBUTES];
        SDL_GPUVertexInputState vertex_input_state = {
            .vertex_buffer_descriptions = buf_desc,
            .num_vertex_buffers = 0,
            .vertex_attributes = attr_desc,   // NOTE pointer
            .num_vertex_attributes = 0
        };
        auto ret_vtx_input = setup_vertex_input(vertex_input_state, buf_desc, attr_desc,
                           render::vertex_type_descriptor::descriptor_table().at(desc.vertex_fmt),
                           res_vert->meta);
        if(!ret_vtx_input)
        {
            log::warn("[render_gpu] pipeline_cache: _create_pipeline: failed to setup vertex input state!");
            return nullptr;
        }


        // rasteriztion controls
        SDL_GPURasterizerState rasterizer_state {};
        setup_material_pipeline_props(rasterizer_state, desc.params);

        // depth state
        SDL_GPUDepthStencilState depth_stencil_state = {
            .compare_op = SDL_GPU_COMPAREOP_LESS,
            .enable_depth_test = desc.params.depth_test,
            .enable_depth_write = desc.params.depth_write,
            .enable_stencil_test = false
        };

        // final assembly
        SDL_GPUGraphicsPipelineCreateInfo create_info = {
            .vertex_shader = vert,
            .fragment_shader = frag,
            .vertex_input_state = vertex_input_state,
            .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
            .rasterizer_state = rasterizer_state,
            .multisample_state = { .sample_count = SDL_GPU_SAMPLECOUNT_1 },
            .depth_stencil_state = depth_stencil_state,
            .target_info = target_info
        };

        auto ret = SDL_CreateGPUGraphicsPipeline(gdev, &create_info);
        if(ret)
        {
            _register_shader_mapping(vert, hash);
            _register_shader_mapping(frag, hash);
        }
        return ret;
    }


    SDL_GPUColorTargetBlendState convert_blend_mode(render::blendmode mode) {
        SDL_GPUColorTargetBlendState state {};
        if (mode == render::blendmode::BLEND) {
            state.enable_blend = true;
            state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        } else if (mode == render::blendmode::ADD) {
            state.enable_blend = true;
            state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        }
        return state;
    }


    void pipeline_cache::_register_shader_mapping(SDL_GPUShader *shader, hash_t hash)
    {
        auto it = m_shader_map.find(shader);
        if(it != m_shader_map.end())
        {
            // entry exists
            // fill inline slots or append to spill vector
            if(it->second.inl_count < SHADER_MAPPING_INLINE_SLOTS)
                it->second.inl[it->second.inl_count++] = hash;
            else
                it->second.spill.push_back(hash);
        }
        else
        {
            // insert a new inline entry
            m_shader_map.emplace(shader, shader_mapping_entry {
                .spill = {},
                .inl = {hash},
                .inl_count = 1
            });
        }
    }

}
