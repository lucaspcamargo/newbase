#pragma once

#include <newbase/res/texture.hpp>
#include <newbase/sys/render_gpu/copy_scope.hpp>
#include <SDL3/SDL_gpu.h>
#include <mutex>

// render_gpu facilities for working with texture resources
// texture_manager is the main point of contact for the renderer

namespace nb::gpu
{

    // fwd-decl
    struct texture_block;

    /// manager structure for shaders in render_gpu
    struct texture_manager
    {
        // TODO instead of creating a sampler for every texture, create a sampler
        //      object cache not dissimilar to what we do with pipelines

        // TODO allow for the cretion of better depth buffers

        /// initializes the texture manager
        /// will query supported textre formats for all usages
        void init(SDL_GPUDevice *dev, bool dump_formats = false);

        /// Attempts to ensure a texture is ready for usage.
        /// The texture resource must be loaded and contain upload data.
        /// The copy pass is optional. If it does not exist, a new one is created.
        /// Using a single copy pass for multiple operations is more optimal.
        /// @returns Whether the texture can be used by the renderer.
        bool prepare(SDL_GPUDevice * dev, SDL_GPUCommandBuffer *cmd, rtexture *tex, SDL_GPUCopyPass *cpy = nullptr);

        /// Prepares multiple textures from an iterator type.
        /// The copy pass is optinal. A new one is created if it does not exist.
        /// @returns Whether all texture can be used by the renderer.
        template<class It>
        bool prepare_multiple(SDL_GPUDevice * dev, SDL_GPUCommandBuffer *cmd, It begin, It end, SDL_GPUCopyPass *cpy = nullptr)
        {
            bool ret = true;
            copy_scope cpy_scope(cmd, cpy);
            for(auto it = begin; it!= end; it++)
                ret = ret && prepare(dev, cmd, &**it, cpy_scope);
            return ret;
        }


        /// Invoked by the renderer at a good time to free resources
        void cleanup(SDL_GPUDevice * dev);

        /// Invoked by the renderer when it is shutting down
        void teardown(SDL_GPUDevice * dev);

        /// sets up a texture resource reference to a GPU texture it does not own
        /// no sampler gets created or set
        /// meant for wrapping a swapchain texture
        void set_non_owning(rtexture *rtex, SDL_GPUTexture *gtex, SDL_GPUTextureFormat gfmt, int w, int h);

        /// ensures the color target texture has the correct dimensions
        /// releases the existing texture and creates a new texture if needed
        /// assumes the target must be sampleable
        /// format for now is fixed at 8-bit RGBA
        bool color_target_setup(SDL_GPUDevice *gdev, rtexture *rtex, int w, int h);

        /// ensures the depth target texture has the correct dimensions
        /// releases the existing texture and creates a new texture if needed
        bool depth_target_setup(SDL_GPUDevice *gdev, rtexture *rtex, int w, int h, bool sampleable);

        /// get gpu texture format name
        static const char * texture_format_to_str(SDL_GPUTextureFormat fmt);


    private:
        // resource destruction callback
        // thread-safe
        static void _texture_destroy(rtexture& tex, void *uptr);

        std::vector<texture_block> m_delete; // deferred delete list
        std::mutex m_delete_mtx;

        struct {
            std::vector<SDL_GPUTextureFormat> samplers;
            std::vector<SDL_GPUTextureFormat> color_targets;
            std::vector<SDL_GPUTextureFormat> depth_stencil_targets;
        } m_support;

        bool m_depth_float {false};
    };


    /// data block for texture gpu data
    struct texture_block
    {
        SDL_GPUTexture *gtex {nullptr};
        SDL_GPUSampler *gsamp {nullptr}; // TODO remove, use sampler cache!!!
        SDL_GPUTextureFormat gfmt {SDL_GPU_TEXTUREFORMAT_INVALID};
        uint32_t w {0};
        uint32_t h {0};
        bool owns_texture {true};
        bool owns_sampler {true};
    };
}
