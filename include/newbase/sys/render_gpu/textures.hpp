#pragma once

#include "newbase/render/pass.hpp"
#include "newbase/render/texture.hpp"
#include <newbase/res/texture.hpp>
#include <newbase/render/target.hpp>
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

        /// Generates mipmaps for textures prepared since the previous call.
        /// Must be called outside of a GPU pass.
        void generate_mipmaps(SDL_GPUCommandBuffer *cmd);

        /// Generates mipmaps for a target texture after it has been rendered to.
        /// Must be called outside of a GPU pass.
        void generate_mipmaps_deferred(SDL_GPUCommandBuffer *cmd, rtexture *tex);

        /// Generates all mip levels for a target texture, regardless of its flags.
        /// Must be called outside of a GPU pass.
        bool generate_mipmaps_for_target(SDL_GPUCommandBuffer *cmd, rtexture *tex);

        /// Prepares multiple textures from an iterator type.
        /// The copy pass is optinal. A new one is created if it does not exist.
        /// @returns Whether all texture can be used by the renderer.
        template<class It>
        bool prepare_multiple(SDL_GPUDevice * dev, SDL_GPUCommandBuffer *cmd, It begin, It end, SDL_GPUCopyPass *cpy = nullptr)
        {
            bool ret = true;
            copy_scope cpy_scope(cmd, cpy);
            for(auto it = begin; it!= end; it++)
            {
                const bool prepared = prepare(dev, cmd, &**it, cpy_scope);
                ret = prepared && ret;
            }
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
        bool color_target_setup(SDL_GPUDevice *gdev, rtexture *rtex, render::texture_format native_fmt, int w, int h);

        /// ensures the depth target texture has the correct dimensions
        /// releases the existing texture and creates a new texture if needed
        bool depth_target_setup(SDL_GPUDevice *gdev, rtexture *rtex, render::texture_format native_fmt, int w, int h, bool sampleable);

        /// converts an engine texture format into a gpu one
        /// engine formats may map to different underlying GPU formats if they
        /// express that they need to be sampleable or targetable
        /// @returns INVALID if no applicable format could be found
        SDL_GPUTextureFormat get_gpu_format(render::texture_format fmt);

        /// converts a gpu texture format into an engine-native format
        /// if a GPU format maps to different native formats with sampling
        /// or targeting hints, the hints may be dropped
        /// @returns INVALID if no applicable format could be found
        render::texture_format get_native_format(SDL_GPUTextureFormat fmt);

        /// get gpu texture format name
        static const char * texture_format_to_str(SDL_GPUTextureFormat fmt);


    private:
        // resource destruction callback
        // thread-safe
        static void _texture_destroy(rtexture& tex, void *uptr);

        std::vector<texture_block> m_delete; // deferred delete list
        std::mutex m_delete_mtx;
        std::vector<rtexture*> m_mipmap_generation;

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
        SDL_GPUTextureFormat gfmt {SDL_GPU_TEXTUREFORMAT_INVALID};
        SDL_GPUTextureType type {SDL_GPU_TEXTURETYPE_2D};
        uint32_t w {0};
        uint32_t h {0};
        uint32_t layer_count_or_depth {1};
        uint32_t num_levels {1};
        bool owns_texture {true};
    };
}
