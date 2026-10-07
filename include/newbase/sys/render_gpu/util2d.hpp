#pragma once

#include "SDL3/SDL_gpu.h"
#include <newbase/sys/render_gpu/pipelines.hpp>
#include <newbase/render/batcher2d.hpp>
#include <newbase/res/shader.hpp>
#include <functional>
#include <memory>

// Utilities to map nb::render 2d data to nb::gpu structures
// Also loads default 2d shaders from core resources

namespace nb::gpu
{

    /// the standard set of shaders used for basic 2d rendering
    struct shaders2d
    {
        std::shared_ptr<rshader> vert {nullptr};
        std::shared_ptr<rshader> frag_col {nullptr};
        std::shared_ptr<rshader> frag_tex {nullptr};

        bool load(SDL_GPUDevice *dev);
        bool prepare(std::function<bool(rshader*)> cb);
        void clear()
        {
            vert.reset();
            frag_col.reset();
            frag_tex.reset();
        }
    };


    // some off-the-cuff estimates for reasonable initial buffer sizes
    // we try to keep it not too small to avoid thrashing
    // but also not too big as to avoiid

    /// Default transfer buffer capacity for 2D batch rendering, in quads
    static constexpr uint32_t DEFAULT_CAPACITY_2D_QUADS = 4096;

    /// Default vertex transfer buffer capacity for 2D batch rendering
    static constexpr uint32_t DEFAULT_CAPACITY_2D_VTX =
        sizeof(render::vertex2d) * DEFAULT_CAPACITY_2D_QUADS * 4;

    /// Default vertex transfer buffer capacity for 2D batch rendering
    static constexpr uint32_t DEFAULT_CAPACITY_2D_IND =
        sizeof(uint16_t) * DEFAULT_CAPACITY_2D_QUADS * 6;


    /// Our structure for handling batcher2d data uploads
    struct buffers2d
    {
        SDL_GPUTransferBuffer *buf_trans_v {nullptr};
        SDL_GPUTransferBuffer *buf_trans_i {nullptr};
        uint32_t cap_trans_v {0};
        uint32_t cap_trans_i {0};
        SDL_GPUBuffer* buf_gpu_v {nullptr};
        SDL_GPUBuffer* buf_gpu_i {nullptr};
        uint32_t buf_gpu_v_sz {0};
        uint32_t buf_gpu_i_sz {0};

        void init(SDL_GPUDevice *dev);
        void teardown(SDL_GPUDevice *dev);
        void upload(SDL_GPUDevice *dev, SDL_GPUCommandBuffer *cmd, const render::batcher2d &batcher, SDL_GPUCopyPass *curr_cpy = nullptr);
    };


    // projects pixel coordinates to clip space
    // column-major to match our shaders
    inline glm::mat4 make_ortho_2d(float width, float height)
    {
        glm::mat4 proj(1.0f);

        // Scale factors
        proj[0][0] =  2.0f / width;
        proj[1][1] =  -2.0f / height;
        proj[2][2] =  1.0f;          // Z mapping [0, 1]

        // Translation offsets
        proj[3][0] = -1.0f;
        proj[3][1] = 1.0f;

        return proj;
    }


    bool command2d_get_pipeline(const render::command2d &cmd, pipeline_desc &target_desc, shaders2d &shaders);

}
