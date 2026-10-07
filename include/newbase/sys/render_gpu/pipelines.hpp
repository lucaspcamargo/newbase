#pragma once

#include "newbase/render/material.hpp"
#include <newbase/render/types.hpp>
#include <newbase/render/shader.hpp>
#include <newbase/render/vertex.hpp>
#include <newbase/utility/mixins.hpp>
#include <SDL3/SDL_gpu.h>
#include <unordered_map>
#include <array>
#include <vector>


// internal render_gpu pipeline management
// unless you are render_gpu, you probably don't need to touch this

namespace nb::gpu
{
    /**
     * A pipeline descriptor contains all the required info
     * needed to create a gpu pipeline.
     * Since they are annoyingly but also usefully explicit,
     * there is a lot of info required.
     *
     * Some of it will come from the rendder material,
     *
     *
     * We'll be adding info here as it proves necessary.
     */
    static constexpr int MAX_VERTEX_BUFFERS = 2;
    static constexpr int MAX_VERTEX_ATTRIBUTES = 8;


    /// The type used as the key for the pipeline cache
    using pipeline_key = uint64_t;

#pragma pack(push,1)
    /// Our pipeline descriptor object
    /// Byte-packed to avoid unused padding bytes ruining
    /// our hashes after normalization.
    /// If you want to reuse the same descriptor object, call
    /// mark_dirty() after changing data to clear caches.
    /// TODO: make this structure more compact by using
    ///       single bytes when the enums fit.
    ///       We could also have ids for vertex buffer and
    ///       attribute formats instead of the whole structs.
    struct pipeline_desc
    {
        // shaders
        SDL_GPUShader *vert {nullptr};
        SDL_GPUShader *frag {nullptr};

        // render targets
        SDL_GPUTextureFormat color_formats[4] {
            SDL_GPU_TEXTUREFORMAT_INVALID,
            SDL_GPU_TEXTUREFORMAT_INVALID,
            SDL_GPU_TEXTUREFORMAT_INVALID,
            SDL_GPU_TEXTUREFORMAT_INVALID
        };
        SDL_GPUTextureFormat depth_format {SDL_GPU_TEXTUREFORMAT_INVALID};
        SDL_GPUSampleCount sample_count {SDL_GPU_SAMPLECOUNT_1};

        // vertex input layout
        SDL_GPUVertexBufferDescription vert_bufs[MAX_VERTEX_BUFFERS];
        SDL_GPUVertexAttribute vert_attrs[MAX_VERTEX_ATTRIBUTES];
        uint16_t vert_buf_count {0};
        uint16_t vert_attr_count {0};

        // rasterization
        SDL_GPUCullMode cull_mode {SDL_GPU_CULLMODE_NONE};
        render::blendmode blend {render::blendmode::NONE};
        bool depth_test {false};
        bool depth_write {false};
        SDL_GPUCompareOp depth_compare_op {SDL_GPU_COMPAREOP_LESS};
        bool depth_clip {true};
        bool depth_bias {false};
        float depth_bias_constant {0.0f};
        float depth_bias_slope {0.0f};
        float depth_bias_clamp {0.0f};

        // utility
        bool setup_vertex_input(
            const render::vertex_type_descriptor& cpu_vertex_desc,
            const render::shader_reflection& vert_shader_reflection);

        void setup_material_pipeline_props(const render::pipeline_params &pp);

        // methods
        bool valid() const;  // whether the pipeline makes sense
        bool normalized() const { return _normalized_flag; }
        void normalize();    // ensure unused data is zeroed-out
        pipeline_key hash(); /// not const because we can cache our hash internally
        void mark_dirty() // call this if reusing a pipeline descriptor after normalization and/or hashing!
        {
            _normalized_flag = false;
            _hash = 0;
        }

    private:
        bool _normalized_flag {false};
        pipeline_key _hash {0};
    };
#pragma pack(pop)


    /**
     * A gpu gfx pipeline object
     * We may get rid of storing the cache key down the line,
     * if we are not using it anywhere
     */
    struct pipeline_object
    {
        // rule fo zero appllies

        // data
        pipeline_key cache_key {0};
        SDL_GPUGraphicsPipeline *pipeline {nullptr};
#ifndef NDEBUG
        pipeline_desc descriptor; // for collision checking, disabled in release
#endif

        // methods
        bool valid() const { return pipeline != nullptr; }
    };


    /**
     * The pipeline cache makes it easy to create and retrieve
     * pipeline objects, using the GPU device and descriptor to
     * retrieve and build them as needed.
     */
    struct pipeline_cache : public nocopy
    {
        /// Takes a pipeline descriptor, and either gets the
        /// pipeline from the cache, or creates and caches it.
        /// The descriptor is normalized before hashing,
        /// to avoid irrelevant data polluting the cache.
        pipeline_object* find_or_create(SDL_GPUDevice *gdev, pipeline_desc& descriptor);

        /// Clears this cache, releasing all pipeline objects in
        /// the process
        void clear(SDL_GPUDevice *gdev);

        /// Removes all pipelines that use this shader from the cache.
        /// Used when a GPU_Shader* instance is deleted.
        /// This is mostly to avoid pointer aliasing, as a new shader
        /// with the same address could conceivably be created.
        /// Not holding onto stale pipelines is also nice.
        void evict_by_shader(SDL_GPUDevice *gdev, SDL_GPUShader *shader);

    private:

        SDL_GPUGraphicsPipeline* _create_pipeline(SDL_GPUDevice *gdev, const pipeline_desc &desc);

        void _register_shader_mapping(SDL_GPUShader *shader, pipeline_key hash);

        std::unordered_map<pipeline_key, pipeline_object> m_cache;

        // reverse shader*->pipeline_id mechanism
        // used by evict_by_shader
        static constexpr uint16_t SHADER_MAPPING_INLINE_SLOTS = 16;
        struct shader_mapping_entry
        {
            std::vector<pipeline_key> spill {};
            std::array<pipeline_key, SHADER_MAPPING_INLINE_SLOTS> inl {};
            std::uint32_t inl_count {0};
        };
        std::unordered_map<SDL_GPUShader*, shader_mapping_entry> m_shader_map;
    };

}
