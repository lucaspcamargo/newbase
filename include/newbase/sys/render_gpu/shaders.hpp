#pragma once

#include <newbase/render/shader.hpp>
#include <newbase/res/shader.hpp>
#include <newbase/sys/render_gpu/pipelines.hpp>
#include <SDL3/SDL_gpu.h>

// render_gpu facilities for working with shader resources
// shader_manager is the main point of contact for the renderer

namespace nb::gpu
{
    /// manager structure for shaders in render_gpu
    struct shader_manager
    {
        /// intialize the shader manager
        void init(SDL_GPUDevice * dev) const {}  // for now, nothing to do

        /// attempts to ensure a shader is ready for usage
        /// the shader resource must be loaded and already contain compiled
        /// bytecode in a format the GPU driver accpts
        /// @returns whether the shader can be used by the renderer
        bool prepare(SDL_GPUDevice * dev, rshader *shader);

        /// Prepares multiple shaders from an iterator type.
        /// @returns Whether all shaders can be used by the renderer.
        template<class It>
        bool prepare_multiple(SDL_GPUDevice * dev, It begin, It end, SDL_GPUCopyPass *cpy = nullptr)
        {
            bool ret = true;
            for(auto it = begin; it!= end; it++)
                ret = ret && prepare(dev, &**it);
            return ret;
        }

        /// Invoked by the renderer at a good time to free resources
        void cleanup(SDL_GPUDevice * dev, gpu::pipeline_cache &pipelines);

        /// Invoked by the renderer when it is shutting down
        void teardown(SDL_GPUDevice * dev, gpu::pipeline_cache &pipelines);

    private:
        // resource destruction callback
        // thread-safe
        static void _shader_destroy(rshader& tex, void *uptr);

        std::vector<SDL_GPUShader*> m_delete; // deferred delete list
        std::mutex m_delete_mtx;
    };



    // convert shader reflection data type to SDL_GPUVertexElementFormat
    inline SDL_GPUVertexElementFormat shader_data_type_to_gpu(nb::render::shader_data_type type)
    {
        using namespace nb::render;
        switch (type) {
            case shader_data_type::FLOAT1: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
            case shader_data_type::FLOAT2: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
            case shader_data_type::FLOAT3: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
            case shader_data_type::FLOAT4: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
            case shader_data_type::INT1:   return SDL_GPU_VERTEXELEMENTFORMAT_INT;
            case shader_data_type::INT2:   return SDL_GPU_VERTEXELEMENTFORMAT_INT2;
            case shader_data_type::INT3:   return SDL_GPU_VERTEXELEMENTFORMAT_INT3;
            case shader_data_type::INT4:   return SDL_GPU_VERTEXELEMENTFORMAT_INT4;
            default:                       return SDL_GPU_VERTEXELEMENTFORMAT_INVALID;
        }
    }

    // get byte size of shader data types to accumulate attribute offsets
    inline uint32_t shader_data_type_size(nb::render::shader_data_type type)
    {
        using namespace nb::render;
        switch (type) {
            case shader_data_type::FLOAT1: return sizeof(float) * 1;
            case shader_data_type::FLOAT2: return sizeof(float) * 2;
            case shader_data_type::FLOAT3: return sizeof(float) * 3;
            case shader_data_type::FLOAT4: return sizeof(float) * 4;
            case shader_data_type::INT1:   return sizeof(int32_t) * 1;
            case shader_data_type::INT2:   return sizeof(int32_t) * 2;
            case shader_data_type::INT3:   return sizeof(int32_t) * 3;
            case shader_data_type::INT4:   return sizeof(int32_t) * 4;
            default:                       return 0;
        }
    }
}
