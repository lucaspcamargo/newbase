#include "newbase/render/vertex.hpp"
#include <newbase/sys/render_gpu/pipelines.hpp>
#include <newbase/log.hpp>
#include <SDL3/SDL_gpu.h>
#include <cstring>
#include <cassert>


namespace nb::gpu
{

    // Discount the sizes of the normalized flag and hash cache
    // NOTE We can only do this because the structure is packed!
    static constexpr auto DESC_DATA_SIZE = sizeof(pipeline_desc) - sizeof(bool) - sizeof(pipeline_key);

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


    bool pipeline_desc::setup_vertex_input(
                        const render::vertex_type_descriptor& cpu_vertex_desc,
                        const render::shader_reflection& vert_shader_reflection)
    {
        // start by clearing out all data
        vert_attr_count = 0;
        vert_buf_count = 0;

        SDL_GPUVertexBufferDescription& buf0 = vert_bufs[vert_buf_count++];
        buf0.slot = 0;
        buf0.pitch = cpu_vertex_desc.size;
        buf0.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        buf0.instance_step_rate = 0;

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
                    SDL_GPUVertexAttribute& attr = vert_attrs[vert_attr_count++];
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

        return true;
    }


    void pipeline_desc::setup_material_pipeline_props(const render::pipeline_params &pp)
    {
        cull_mode = pp.cull_back? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE;
        blend = pp.blend;
        depth_test = pp.depth_test;
        depth_write = pp.depth_write;
    }

    bool pipeline_desc::valid() const
    {
        // a nomalized descriptor is always valid
        if(normalized())
            return true;

        if(!vert || !frag)
            return false;  // need shaders

        if(!vert_buf_count || !vert_attr_count)
            return false; // needs vertices with attributes

        if(vert_buf_count > MAX_VERTEX_BUFFERS || vert_attr_count > MAX_VERTEX_ATTRIBUTES)
            return false; // needs vertices with attributes

        // need at least one valid target to write to
        if(color_formats[0] == SDL_GPU_TEXTUREFORMAT_INVALID && depth_format == SDL_GPU_TEXTUREFORMAT_INVALID)
            return false;

        return true;
    }

    void pipeline_desc::normalize()
    {
        // control block to prevent normalizing over and over
        if(normalized())
            return;

        if(!valid())
            return;

        _normalized_flag = true;

        // clear unused vertex buffer and attribute slots
        memset(vert_bufs + vert_buf_count, 0, sizeof(SDL_GPUVertexBufferDescription)*(MAX_VERTEX_BUFFERS-vert_buf_count));
        memset(vert_attrs + vert_attr_count, 0, sizeof(SDL_GPUVertexAttribute)*(MAX_VERTEX_ATTRIBUTES-vert_attr_count));

        if (depth_format == SDL_GPU_TEXTUREFORMAT_INVALID) {
            depth_test = false;
            depth_write = false;
            depth_compare_op = (SDL_GPUCompareOp)0;
        }
        else if (!depth_test && !depth_write) {
            // If testing and writing are both off, compare_op doesn't affect rendering
            depth_compare_op = (SDL_GPUCompareOp)0;
        }

        if (!depth_bias || depth_format == SDL_GPU_TEXTUREFORMAT_INVALID) {
            depth_bias = false;
            depth_bias_constant = 0.0f;
            depth_bias_slope = 0.0f;
            depth_bias_clamp = 0.0f;
        }

    }

    pipeline_key pipeline_desc::hash()
    {
        if(_hash != 0)
            return _hash;

        normalize();

        // in case the descriptor is invalid,
        // normalization fails
        if(!normalized())
            return 0;

        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(this);
        // 64-bit FNV
        pipeline_key hash = 14695981039346656037ULL;
        for (size_t i = 0; i < DESC_DATA_SIZE; ++i) {
            hash ^= bytes[i];
            hash *= 1099511628211ULL; // prime
        }
        _hash = hash;
        return hash;
    }

    // collision detector
    bool operator==(const pipeline_desc& a, const pipeline_desc& b) {
        return std::memcmp(&a, &b, DESC_DATA_SIZE) == 0;
    }

    pipeline_object* pipeline_cache::find_or_create(SDL_GPUDevice *gdev, pipeline_desc& descriptor)
    {
        assert(gdev && "[pipeline_cache] no GPU device!");

        auto hash = descriptor.hash();
        if(!hash)
            return nullptr; // invalid descriptor!

        auto it = m_cache.find(hash);
        if (it != m_cache.end()) {
#ifndef NDEBUG
            if (it->second.descriptor != descriptor) {
                assert(!"CRITICAL: 64-bit pipeline hash collision detected!");
            }
#endif
            return &it->second;
        }

        auto gpu_pipeline = _create_pipeline(gdev, descriptor);
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

        _register_shader_mapping(descriptor.vert, hash);
        _register_shader_mapping(descriptor.frag, hash);

        // return address of newly-created pipeline object
        return &(new_it.first->second);
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


    SDL_GPUGraphicsPipeline* pipeline_cache::_create_pipeline(SDL_GPUDevice *gdev, const pipeline_desc &desc)
    {
        // color targets prep
        SDL_GPUColorTargetDescription color_targets[4]{};
        uint32_t active_color_targets = 0;
        for (uint32_t i = 0; i < 4; ++i) {
            if (desc.color_formats[i] != SDL_GPU_TEXTUREFORMAT_INVALID) {
                color_targets[i].format = desc.color_formats[i];

                // Configure blend state if enabled
                if (desc.blend != render::blendmode::NONE) {
                    color_targets[i].blend_state = convert_blend_mode(desc.blend);
                }
                active_color_targets++;
            }
        }

        // render target info
        SDL_GPUGraphicsPipelineTargetInfo target_info = {
            .color_target_descriptions = color_targets,  // NOTE pointer
            .num_color_targets = active_color_targets,
            .depth_stencil_format = desc.depth_format,
            .has_depth_stencil_target = (desc.depth_format != SDL_GPU_TEXTUREFORMAT_INVALID)
        };

        // vertex input, taken verbatim
        SDL_GPUVertexInputState vertex_input_state = {
            .vertex_buffer_descriptions = desc.vert_bufs, // NOTE pointer
            .num_vertex_buffers = desc.vert_buf_count,
            .vertex_attributes = desc.vert_attrs,   // NOTE pointer
            .num_vertex_attributes = desc.vert_attr_count
        };

        // rasteriztion controls
        SDL_GPURasterizerState rasterizer_state = {
            .fill_mode = SDL_GPU_FILLMODE_FILL,
            .cull_mode = desc.cull_mode,
            .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
            .depth_bias_constant_factor = desc.depth_bias_constant,
            .depth_bias_clamp = desc.depth_bias_clamp,
            .depth_bias_slope_factor = desc.depth_bias_slope,
            .enable_depth_bias = desc.depth_bias,
            .enable_depth_clip = desc.depth_clip
        };

        // depth state
        SDL_GPUDepthStencilState depth_stencil_state = {
            .compare_op = desc.depth_compare_op,
            .enable_depth_test = desc.depth_test,
            .enable_depth_write = desc.depth_write,
            .enable_stencil_test = false
        };

        // final assembly
        SDL_GPUGraphicsPipelineCreateInfo create_info = {
            .vertex_shader = desc.vert,
            .fragment_shader = desc.frag,
            .vertex_input_state = vertex_input_state,
            .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
            .rasterizer_state = rasterizer_state,
            .multisample_state = { .sample_count = desc.sample_count },
            .depth_stencil_state = depth_stencil_state,
            .target_info = target_info
        };

        return SDL_CreateGPUGraphicsPipeline(gdev, &create_info);
    }


    SDL_GPUColorTargetBlendState convert_blend_mode(render::blendmode mode) {
        SDL_GPUColorTargetBlendState state{};
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


    void pipeline_cache::_register_shader_mapping(SDL_GPUShader *shader, pipeline_key hash)
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
