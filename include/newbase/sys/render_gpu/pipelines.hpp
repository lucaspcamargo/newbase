#pragma once

#include "newbase/render/material.hpp"
#include "newbase/render/pipeline.hpp"
#include "newbase/sys/render_gpu/targets.hpp"
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

    /**
     * A gpu gfx pipeline object
     * We may get rid of storing the cache key down the line,
     * if we are not using it anywhere
     */
    struct pipeline_object
    {
        // rule fo zero appllies

        // data
        render::pipeline_descriptor::hash_t cache_key {0};
        SDL_GPUGraphicsPipeline *pipeline {nullptr};
#ifndef NDEBUG
        render::pipeline_descriptor descriptor; // for collision checking, disabled in release
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
        /// constructor. takes a target_manager for translating
        /// engine-native pass_taret_info into SDL_GPU structs
        pipeline_cache(target_manager &tg) : m_targets(tg) {}

        using descriptor_t = render::pipeline_descriptor;
        using hash_t = render::pipeline_descriptor::hash_t;

        /// Takes a pipeline descriptor, and either gets the
        /// pipeline from the cache, or creates and caches it.
        /// The descriptor is normalized before hashing,
        /// to avoid irrelevant data polluting the cache.
        pipeline_object* find_or_create(SDL_GPUDevice *gdev, const descriptor_t& descriptor);

        /// Same as find_or_create, but takes a precalculated hash
        pipeline_object* find_or_create_prehashed(SDL_GPUDevice *gdev, const descriptor_t& descriptor,
                                                  hash_t hash);


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
        target_manager& m_targets;

        SDL_GPUGraphicsPipeline* _create_pipeline(SDL_GPUDevice *gdev, const descriptor_t &desc, hash_t hash);

        void _register_shader_mapping(SDL_GPUShader *shader, hash_t hash);

        std::unordered_map<hash_t, pipeline_object> m_cache;

        // reverse shader*->pipeline_id mechanism
        // used by evict_by_shader
        static constexpr uint16_t SHADER_MAPPING_INLINE_SLOTS = 16;
        struct shader_mapping_entry
        {
            std::vector<hash_t> spill {};
            std::array<hash_t, SHADER_MAPPING_INLINE_SLOTS> inl {};
            std::uint32_t inl_count {0};
        };
        std::unordered_map<SDL_GPUShader*, shader_mapping_entry> m_shader_map;
    };

}
